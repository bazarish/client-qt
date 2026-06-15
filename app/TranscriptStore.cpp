// Bazarish project (c) 2026
#include "TranscriptStore.hpp"

#include <bazarish/Bytes.hpp>
#include <bazarish/Cms.hpp>

#include <QSqlDatabase>
#include <QSqlDriver>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

#include <sqlite3.h>

#include <fstream>
#include <stdexcept>

namespace bazarish::app {

namespace {

// Returns the underlying SQLite C handle for an open Qt connection, so the
// in-memory database can be serialized/deserialized. Throws if the driver does
// not expose one (always the QSQLITE driver here).
sqlite3* sqliteHandle(const QSqlDatabase& db)
{
    const QVariant handle = db.driver()->handle();
    if (!handle.isValid() || qstrcmp(handle.typeName(), "sqlite3*") != 0) {
        throw std::runtime_error("transcript: no SQLite handle on the connection");
    }
    sqlite3* const native = *static_cast<sqlite3* const*>(handle.constData());
    if (native == nullptr) {
        throw std::runtime_error("transcript: null SQLite handle");
    }
    return native;
}

Bytes readFileBytes(const QString& path)
{
    std::ifstream in(path.toStdString(), std::ios::binary);
    return Bytes(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

void writeFileBytes(const QString& path, const Bytes& bytes)
{
    std::ofstream out(path.toStdString(), std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!out) {
        throw std::runtime_error("transcript: failed to write sealed database");
    }
}

}  // namespace

TranscriptStore::TranscriptStore() = default;

TranscriptStore::~TranscriptStore()
{
    if (!connectionName_.isEmpty()) {
        flush();
        QSqlDatabase::database(connectionName_).close();
        QSqlDatabase::removeDatabase(connectionName_);
    }
}

bool TranscriptStore::open(const QString& profileId, const QString& dbPath, const QString& passphrase)
{
    connectionName_ = "transcript-" + profileId;
    encrypted_ = !passphrase.isEmpty();
    passphrase_ = passphrase.toStdString();
    blobPath_ = dbPath + ".enc";

    QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", connectionName_);
    // Encrypted profiles keep the database off disk: an in-memory connection
    // loaded from / saved to the sealed blob. Unencrypted profiles use a plain
    // file as before.
    db.setDatabaseName(encrypted_ ? QStringLiteral(":memory:") : dbPath);
    if (!db.open()) {
        return false;
    }

    if (encrypted_ && std::ifstream(blobPath_.toStdString(), std::ios::binary).good()) {
        const Bytes sealed = readFileBytes(blobPath_);
        const Bytes plain = cms::unsealWithPassword(sealed, passphrase_);
        // SQLite takes ownership of the buffer (FREEONCLOSE) and may grow it
        // (RESIZEABLE), so it must be a sqlite3_malloc allocation.
        unsigned char* const buffer
            = static_cast<unsigned char*>(sqlite3_malloc64(plain.empty() ? 1 : plain.size()));
        if (buffer == nullptr) {
            throw std::runtime_error("transcript: sqlite3_malloc64 failed");
        }
        std::copy(plain.begin(), plain.end(), buffer);
        if (sqlite3_deserialize(sqliteHandle(db), "main", buffer,
                static_cast<sqlite3_int64>(plain.size()), static_cast<sqlite3_int64>(plain.size()),
                SQLITE_DESERIALIZE_FREEONCLOSE | SQLITE_DESERIALIZE_RESIZEABLE)
            != SQLITE_OK) {
            throw std::runtime_error("transcript: sqlite3_deserialize failed");
        }
    }

    QSqlQuery query(db);
    if (!query.exec(
            "CREATE TABLE IF NOT EXISTS messages ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "peer TEXT NOT NULL, outgoing INTEGER, type TEXT, sender TEXT, protocolId TEXT,"
            "text TEXT, attName TEXT, attMime TEXT, attSize INTEGER,"
            "attRef TEXT, attKey TEXT, keyboard TEXT, edited INTEGER, ts INTEGER, status INTEGER)")) {
        return false;
    }
    ready_ = true;
    return true;
}

void TranscriptStore::flush() const
{
    if (!encrypted_ || !ready_) {
        return;
    }
    const QSqlDatabase db = QSqlDatabase::database(connectionName_);
    sqlite3_int64 size = 0;
    unsigned char* const data = sqlite3_serialize(sqliteHandle(db), "main", &size, 0);
    if (data == nullptr) {
        throw std::runtime_error("transcript: sqlite3_serialize failed");
    }
    const Bytes plain(data, data + size);
    sqlite3_free(data);
    writeFileBytes(blobPath_, cms::sealWithPassword(plain, passphrase_));
}

qint64 TranscriptStore::append(const StoredMessage& message)
{
    QSqlQuery query(QSqlDatabase::database(connectionName_));
    query.prepare(
        "INSERT INTO messages (peer, outgoing, type, sender, protocolId, text, attName, attMime,"
        " attSize, attRef, attKey, keyboard, edited, ts, status)"
        " VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)");
    query.addBindValue(message.peer);
    query.addBindValue(message.outgoing ? 1 : 0);
    query.addBindValue(message.type);
    query.addBindValue(message.sender);
    query.addBindValue(message.protocolId);
    query.addBindValue(message.text);
    query.addBindValue(message.attName);
    query.addBindValue(message.attMime);
    query.addBindValue(message.attSize);
    query.addBindValue(message.attRef);
    query.addBindValue(message.attKey);
    query.addBindValue(message.keyboard);
    query.addBindValue(message.edited ? 1 : 0);
    query.addBindValue(message.ts);
    query.addBindValue(message.status);
    if (!query.exec()) {
        return 0;
    }
    const qint64 id = query.lastInsertId().toLongLong();
    flush();
    return id;
}

void TranscriptStore::updateStatus(qint64 id, int status)
{
    QSqlQuery query(QSqlDatabase::database(connectionName_));
    query.prepare("UPDATE messages SET status = ? WHERE id = ?");
    query.addBindValue(status);
    query.addBindValue(id);
    if (query.exec()) {
        flush();
    }
}

QVector<StoredMessage> TranscriptStore::messagesFor(const QString& peer) const
{
    QVector<StoredMessage> result;
    QSqlQuery query(QSqlDatabase::database(connectionName_));
    query.prepare("SELECT id, peer, outgoing, type, sender, protocolId, text, attName, attMime,"
                  " attSize, attRef, attKey, keyboard, edited, ts, status FROM messages"
                  " WHERE peer = ? ORDER BY id");
    query.addBindValue(peer);
    if (!query.exec()) {
        return result;
    }
    while (query.next()) {
        StoredMessage m;
        m.id = query.value(0).toLongLong();
        m.peer = query.value(1).toString();
        m.outgoing = query.value(2).toInt() != 0;
        m.type = query.value(3).toString();
        m.sender = query.value(4).toString();
        m.protocolId = query.value(5).toString();
        m.text = query.value(6).toString();
        m.attName = query.value(7).toString();
        m.attMime = query.value(8).toString();
        m.attSize = query.value(9).toLongLong();
        m.attRef = query.value(10).toString();
        m.attKey = query.value(11).toString();
        m.keyboard = query.value(12).toString();
        m.edited = query.value(13).toInt() != 0;
        m.ts = query.value(14).toLongLong();
        m.status = query.value(15).toInt();
        result.push_back(m);
    }
    return result;
}

qint64 TranscriptStore::idForProtocol(const QString& protocolId) const
{
    if (protocolId.isEmpty()) {
        return 0;
    }
    QSqlQuery query(QSqlDatabase::database(connectionName_));
    query.prepare("SELECT id FROM messages WHERE protocolId = ? AND outgoing = 1 LIMIT 1");
    query.addBindValue(protocolId);
    if (query.exec() && query.next()) {
        return query.value(0).toLongLong();
    }
    return 0;
}

qint64 TranscriptStore::idForIncomingProtocol(const QString& protocolId, const QString& peer) const
{
    if (protocolId.isEmpty()) {
        return 0;
    }
    QSqlQuery query(QSqlDatabase::database(connectionName_));
    query.prepare(
        "SELECT id FROM messages WHERE protocolId = ? AND peer = ? AND outgoing = 0 LIMIT 1");
    query.addBindValue(protocolId);
    query.addBindValue(peer);
    if (query.exec() && query.next()) {
        return query.value(0).toLongLong();
    }
    return 0;
}

void TranscriptStore::editContent(qint64 id, const QString& text, const QString& keyboard)
{
    QSqlQuery query(QSqlDatabase::database(connectionName_));
    query.prepare("UPDATE messages SET text = ?, keyboard = ?, edited = 1 WHERE id = ?");
    query.addBindValue(text);
    query.addBindValue(keyboard);
    query.addBindValue(id);
    if (query.exec()) {
        flush();
    }
}

QString TranscriptStore::lastText(const QString& peer) const
{
    QSqlQuery query(QSqlDatabase::database(connectionName_));
    query.prepare("SELECT text, type FROM messages WHERE peer = ? ORDER BY id DESC LIMIT 1");
    query.addBindValue(peer);
    if (query.exec() && query.next()) {
        const QString text = query.value(0).toString();
        const QString type = query.value(1).toString();
        if (!text.isEmpty()) {
            return text;
        }
        if (type == "file" || type == "photo" || type == "audio" || type == "voice") {
            return "[" + type + "]";
        }
    }
    return {};
}

qint64 TranscriptStore::lastTime(const QString& peer) const
{
    QSqlQuery query(QSqlDatabase::database(connectionName_));
    query.prepare("SELECT ts FROM messages WHERE peer = ? ORDER BY id DESC LIMIT 1");
    query.addBindValue(peer);
    if (query.exec() && query.next()) {
        return query.value(0).toLongLong();
    }
    return 0;
}

}  // namespace bazarish::app
