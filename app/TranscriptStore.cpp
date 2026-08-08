// Bazarish project (c) 2026
#include "TranscriptStore.hpp"

#include <bazarish/Bytes.hpp>
#include <bazarish/Cms.hpp>

#include <QSqlDatabase>
#include <QSqlDriver>
#include <QSqlError>
#include <QSqlQuery>
#include <QStringList>
#include <QVariant>

#include <sqlite3.h>

#include <algorithm>
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

// Column list shared by every full-row query, so the indices below stay aligned.
// orderKey is appended last so the existing 0..16 indices are unchanged.
const char* const kMessageColumns = "id, peer, outgoing, type, protocolId, text, attName,"
                                    " attMime, attSize, attRef, attKey, attSrcPath, keyboard,"
                                    " edited, ts, status, orderKey, savedPath, blobGone, replyTo";

// Orders a loaded window oldest-first by the sort position (orderKey), then id as
// a stable tiebreak. Each window is a contiguous id-range, so this repairs an
// out-of-order burst within the window; orderKey is near-monotonic with id, so a
// burst straddling a page boundary is at most a hair off (see Messages.md).
void sortByOrder(QVector<StoredMessage>& rows)
{
    std::sort(rows.begin(), rows.end(), [](const StoredMessage& a, const StoredMessage& b) {
        if (a.orderKey != b.orderKey) {
            return a.orderKey < b.orderKey;
        }
        return a.id < b.id;
    });
}

// Reads one row produced by a SELECT over kMessageColumns into a StoredMessage.
StoredMessage readMessageRow(const QSqlQuery& query)
{
    StoredMessage m;
    m.id = query.value(0).toLongLong();
    m.peer = query.value(1).toString();
    m.outgoing = query.value(2).toInt() != 0;
    m.type = query.value(3).toString();
    m.protocolId = query.value(4).toString();
    m.text = query.value(5).toString();
    m.attName = query.value(6).toString();
    m.attMime = query.value(7).toString();
    m.attSize = query.value(8).toLongLong();
    m.attRef = query.value(9).toString();
    m.attKey = query.value(10).toString();
    m.attSrcPath = query.value(11).toString();
    m.keyboard = query.value(12).toString();
    m.edited = query.value(13).toInt() != 0;
    m.ts = query.value(14).toLongLong();
    m.status = query.value(15).toInt();
    m.orderKey = query.value(16).toLongLong();
    m.savedPath = query.value(17).toString();
    m.blobGone = query.value(18).toInt() != 0;
    m.replyTo = query.value(19).toString();
    return m;
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
            "peer TEXT NOT NULL, outgoing INTEGER, type TEXT, protocolId TEXT,"
            "text TEXT, attName TEXT, attMime TEXT, attSize INTEGER,"
            "attRef TEXT, attKey TEXT, attSrcPath TEXT, keyboard TEXT, edited INTEGER,"
            " ts INTEGER, status INTEGER, orderKey INTEGER, savedPath TEXT,"
            " blobGone INTEGER, replyTo TEXT)")) {
        return false;
    }
    // Per-peer read high-water for persistent unread tracking (see read state).
    if (!query.exec("CREATE TABLE IF NOT EXISTS read_state ("
                    "peer TEXT PRIMARY KEY, last_read_id INTEGER NOT NULL)")) {
        return false;
    }
    // One reaction per (peer, message, reactor): a new emoji overwrites the old.
    if (!query.exec("CREATE TABLE IF NOT EXISTS reactions ("
                    "peer TEXT, target TEXT, reactor TEXT, emoji TEXT,"
                    " PRIMARY KEY (peer, target, reactor))")) {
        return false;
    }
    // Chats the user pinned to the top of the list (one row per pinned peer).
    if (!query.exec("CREATE TABLE IF NOT EXISTS pinned_chats (peer TEXT PRIMARY KEY)")) {
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
        "INSERT INTO messages (peer, outgoing, type, protocolId, text, attName, attMime,"
        " attSize, attRef, attKey, attSrcPath, keyboard, edited, ts, status, orderKey, replyTo)"
        " VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)");
    query.addBindValue(message.peer);
    query.addBindValue(message.outgoing ? 1 : 0);
    query.addBindValue(message.type);
    query.addBindValue(message.protocolId);
    query.addBindValue(message.text);
    query.addBindValue(message.attName);
    query.addBindValue(message.attMime);
    query.addBindValue(message.attSize);
    query.addBindValue(message.attRef);
    query.addBindValue(message.attKey);
    query.addBindValue(message.attSrcPath);
    query.addBindValue(message.keyboard);
    query.addBindValue(message.edited ? 1 : 0);
    query.addBindValue(message.ts);
    query.addBindValue(message.status);
    query.addBindValue(message.orderKey);
    query.addBindValue(message.replyTo);
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
    query.prepare(QStringLiteral("SELECT %1 FROM messages WHERE peer = ? ORDER BY id")
                      .arg(kMessageColumns));
    query.addBindValue(peer);
    if (!query.exec()) {
        return result;
    }
    while (query.next()) {
        result.push_back(readMessageRow(query));
    }
    sortByOrder(result);
    return result;
}

QVector<StoredMessage> TranscriptStore::latestMessages(const QString& peer, int limit) const
{
    // Newest `limit` rows, returned oldest-first (the display order). DESC+LIMIT
    // reads only the tail of a huge conversation; the result is then reversed.
    QVector<StoredMessage> result;
    QSqlQuery query(QSqlDatabase::database(connectionName_));
    query.prepare(QStringLiteral("SELECT %1 FROM messages WHERE peer = ? ORDER BY id DESC LIMIT ?")
                      .arg(kMessageColumns));
    query.addBindValue(peer);
    query.addBindValue(limit);
    if (!query.exec()) {
        return result;
    }
    while (query.next()) {
        result.push_back(readMessageRow(query));
    }
    sortByOrder(result);
    return result;
}

QVector<StoredMessage> TranscriptStore::olderMessages(
    const QString& peer, qint64 beforeId, int limit) const
{
    // The `limit` rows immediately older than beforeId, oldest-first.
    QVector<StoredMessage> result;
    QSqlQuery query(QSqlDatabase::database(connectionName_));
    query.prepare(QStringLiteral("SELECT %1 FROM messages WHERE peer = ? AND id < ?"
                                 " ORDER BY id DESC LIMIT ?")
                      .arg(kMessageColumns));
    query.addBindValue(peer);
    query.addBindValue(beforeId);
    query.addBindValue(limit);
    if (!query.exec()) {
        return result;
    }
    while (query.next()) {
        result.push_back(readMessageRow(query));
    }
    sortByOrder(result);
    return result;
}

QVector<StoredMessage> TranscriptStore::newerMessages(
    const QString& peer, qint64 afterId, int limit) const
{
    // The `limit` rows immediately newer than afterId, already oldest-first.
    QVector<StoredMessage> result;
    QSqlQuery query(QSqlDatabase::database(connectionName_));
    query.prepare(QStringLiteral("SELECT %1 FROM messages WHERE peer = ? AND id > ?"
                                 " ORDER BY id ASC LIMIT ?")
                      .arg(kMessageColumns));
    query.addBindValue(peer);
    query.addBindValue(afterId);
    query.addBindValue(limit);
    if (!query.exec()) {
        return result;
    }
    while (query.next()) {
        result.push_back(readMessageRow(query));
    }
    sortByOrder(result);
    return result;
}

bool TranscriptStore::hasMessagesBefore(const QString& peer, qint64 id) const
{
    QSqlQuery query(QSqlDatabase::database(connectionName_));
    query.prepare("SELECT 1 FROM messages WHERE peer = ? AND id < ? LIMIT 1");
    query.addBindValue(peer);
    query.addBindValue(id);
    return query.exec() && query.next();
}

bool TranscriptStore::hasMessagesAfter(const QString& peer, qint64 id) const
{
    QSqlQuery query(QSqlDatabase::database(connectionName_));
    query.prepare("SELECT 1 FROM messages WHERE peer = ? AND id > ? LIMIT 1");
    query.addBindValue(peer);
    query.addBindValue(id);
    return query.exec() && query.next();
}

QVector<SearchHit> TranscriptStore::searchInPeer(const QString& peer, const QString& query) const
{
    // Full-text scan of a conversation's text, newest first. The match is done in
    // C++ so it is case-insensitive for non-ASCII (Cyrillic) too, which SQLite's
    // LIKE/lower() is not. Capped so a degenerate query cannot flood the popup.
    QVector<SearchHit> hits;
    if (query.isEmpty()) {
        return hits;
    }
    QSqlQuery sql(QSqlDatabase::database(connectionName_));
    sql.prepare("SELECT id, ts, text, outgoing, attName FROM messages"
                " WHERE peer = ? AND (text <> '' OR attName <> '') ORDER BY id DESC");
    sql.addBindValue(peer);
    if (!sql.exec()) {
        return hits;
    }
    constexpr int kMaxHits = 500;
    while (sql.next() && hits.size() < kMaxHits) {
        const QString text = sql.value(2).toString();
        const QString attName = sql.value(4).toString();
        // Match the message text or, for an attachment, its file name.
        if (!text.contains(query, Qt::CaseInsensitive)
            && !attName.contains(query, Qt::CaseInsensitive)) {
            continue;
        }
        SearchHit hit;
        hit.id = sql.value(0).toLongLong();
        hit.ts = sql.value(1).toLongLong();
        // Show the message text, or the file name (with a paperclip) for an
        // attachment, so a file hit reads as a file in the results.
        hit.text = !text.isEmpty() ? text : (QStringLiteral("📎 ") + attName);
        hit.outgoing = sql.value(3).toInt() != 0;
        hits.push_back(hit);
    }
    return hits;
}

int TranscriptStore::failUnsentOnLoad(int sendingStatus, int failedStatus)
{
    QSqlQuery query(QSqlDatabase::database(connectionName_));
    query.prepare("UPDATE messages SET status = ? WHERE outgoing = 1 AND status = ?");
    query.addBindValue(failedStatus);
    query.addBindValue(sendingStatus);
    if (!query.exec()) {
        return 0;
    }
    const int changed = query.numRowsAffected();
    if (changed > 0) {
        flush();
    }
    return changed;
}

void TranscriptStore::markOutgoingReadUpTo(
    const QString& peer, qint64 uptoId, int readStatus, int minStatus, int maxStatus)
{
    QSqlQuery query(QSqlDatabase::database(connectionName_));
    query.prepare("UPDATE messages SET status = ? WHERE outgoing = 1 AND peer = ? AND id <= ?"
                  " AND status >= ? AND status <= ?");
    query.addBindValue(readStatus);
    query.addBindValue(peer);
    query.addBindValue(uptoId);
    query.addBindValue(minStatus);
    query.addBindValue(maxStatus);
    if (query.exec() && query.numRowsAffected() > 0) {
        flush();
    }
}

QString TranscriptStore::sourcePathFor(qint64 id) const
{
    QSqlQuery query(QSqlDatabase::database(connectionName_));
    query.prepare("SELECT attSrcPath FROM messages WHERE id = ? LIMIT 1");
    query.addBindValue(id);
    if (query.exec() && query.next()) {
        return query.value(0).toString();
    }
    return {};
}

void TranscriptStore::setSavedPath(qint64 id, const QString& path)
{
    QSqlQuery query(QSqlDatabase::database(connectionName_));
    query.prepare("UPDATE messages SET savedPath = ? WHERE id = ?");
    query.addBindValue(path);
    query.addBindValue(id);
    if (query.exec()) {
        flush();
    }
}

void TranscriptStore::setBlobGone(qint64 id, bool gone)
{
    QSqlQuery query(QSqlDatabase::database(connectionName_));
    query.prepare("UPDATE messages SET blobGone = ? WHERE id = ?");
    query.addBindValue(gone ? 1 : 0);
    query.addBindValue(id);
    if (query.exec()) {
        flush();
    }
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

qint64 TranscriptStore::idForAnyProtocol(const QString& protocolId, const QString& peer) const
{
    if (protocolId.isEmpty()) {
        return 0;
    }
    QSqlQuery query(QSqlDatabase::database(connectionName_));
    query.prepare("SELECT id FROM messages WHERE protocolId = ? AND peer = ? ORDER BY id LIMIT 1");
    query.addBindValue(protocolId);
    query.addBindValue(peer);
    if (query.exec() && query.next()) {
        return query.value(0).toLongLong();
    }
    return 0;
}

StoredMessage TranscriptStore::messageByProtocol(
    const QString& protocolId, const QString& peer) const
{
    StoredMessage m;
    if (protocolId.isEmpty()) {
        return m;
    }
    QSqlQuery query(QSqlDatabase::database(connectionName_));
    query.prepare(QStringLiteral("SELECT %1 FROM messages WHERE protocolId = ? AND peer = ?"
                                 " ORDER BY id LIMIT 1")
                      .arg(kMessageColumns));
    query.addBindValue(protocolId);
    query.addBindValue(peer);
    if (query.exec() && query.next()) {
        m = readMessageRow(query);
    }
    return m;
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

void TranscriptStore::removeById(qint64 id)
{
    QSqlQuery query(QSqlDatabase::database(connectionName_));
    query.prepare("DELETE FROM messages WHERE id = ?");
    query.addBindValue(id);
    if (query.exec()) {
        flush();
    }
}

void TranscriptStore::clearPeer(const QString& peer)
{
    QSqlQuery query(QSqlDatabase::database(connectionName_));
    query.prepare("DELETE FROM messages WHERE peer = ?");
    query.addBindValue(peer);
    if (query.exec()) {
        flush();
    }
}

QStringList TranscriptStore::conversationPeers() const
{
    QStringList peers;
    QSqlQuery query(QSqlDatabase::database(connectionName_));
    if (query.exec("SELECT DISTINCT peer FROM messages")) {
        while (query.next()) {
            peers << query.value(0).toString();
        }
    }
    return peers;
}

QString TranscriptStore::lastText(const QString& peer) const
{
    QSqlQuery query(QSqlDatabase::database(connectionName_));
    query.prepare(
        "SELECT text, type FROM messages WHERE peer = ? ORDER BY orderKey DESC, id DESC LIMIT 1");
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
    query.prepare(
        "SELECT ts FROM messages WHERE peer = ? ORDER BY orderKey DESC, id DESC LIMIT 1");
    query.addBindValue(peer);
    if (query.exec() && query.next()) {
        return query.value(0).toLongLong();
    }
    return 0;
}

void TranscriptStore::setLastReadId(const QString& peer, qint64 id)
{
    if (peer.isEmpty() || id <= 0) {
        return;
    }
    QSqlQuery query(QSqlDatabase::database(connectionName_));
    // Upsert, but never lower the high-water (a re-read of older history must not
    // resurrect newer messages as unread).
    query.prepare("INSERT INTO read_state (peer, last_read_id) VALUES (?, ?)"
                  " ON CONFLICT(peer) DO UPDATE SET last_read_id = max(last_read_id, excluded.last_read_id)");
    query.addBindValue(peer);
    query.addBindValue(id);
    if (query.exec() && query.numRowsAffected() > 0) {
        flush();
    }
}

qint64 TranscriptStore::lastReadId(const QString& peer) const
{
    QSqlQuery query(QSqlDatabase::database(connectionName_));
    query.prepare("SELECT last_read_id FROM read_state WHERE peer = ? LIMIT 1");
    query.addBindValue(peer);
    if (query.exec() && query.next()) {
        return query.value(0).toLongLong();
    }
    return 0;
}

void TranscriptStore::setPinned(const QString& peer, bool pinned)
{
    if (peer.isEmpty()) {
        return;
    }
    QSqlQuery query(QSqlDatabase::database(connectionName_));
    if (pinned) {
        query.prepare("INSERT OR IGNORE INTO pinned_chats (peer) VALUES (?)");
    } else {
        query.prepare("DELETE FROM pinned_chats WHERE peer = ?");
    }
    query.addBindValue(peer);
    if (query.exec() && query.numRowsAffected() > 0) {
        flush();
    }
}

bool TranscriptStore::isPinned(const QString& peer) const
{
    QSqlQuery query(QSqlDatabase::database(connectionName_));
    query.prepare("SELECT 1 FROM pinned_chats WHERE peer = ? LIMIT 1");
    query.addBindValue(peer);
    return query.exec() && query.next();
}

QStringList TranscriptStore::pinnedPeers() const
{
    QStringList peers;
    QSqlQuery query(QSqlDatabase::database(connectionName_));
    if (query.exec("SELECT peer FROM pinned_chats")) {
        while (query.next()) {
            peers << query.value(0).toString();
        }
    }
    return peers;
}

int TranscriptStore::unreadCount(const QString& peer) const
{
    QSqlQuery query(QSqlDatabase::database(connectionName_));
    // Incoming messages newer than the read high-water. Only inbound rows count
    // (our own messages are always "read"), and locally-generated service banners
    // (type 'system', e.g. "X cleared the chat") are not messages to be read.
    query.prepare("SELECT COUNT(*) FROM messages WHERE peer = ? AND outgoing = 0 AND type != 'system' AND id >"
                  " (SELECT COALESCE(MAX(last_read_id), 0) FROM read_state WHERE peer = ?)");
    query.addBindValue(peer);
    query.addBindValue(peer);
    if (query.exec() && query.next()) {
        return query.value(0).toInt();
    }
    return 0;
}

qint64 TranscriptStore::firstUnreadId(const QString& peer) const
{
    QSqlQuery query(QSqlDatabase::database(connectionName_));
    query.prepare("SELECT MIN(id) FROM messages WHERE peer = ? AND outgoing = 0 AND type != 'system' AND id >"
                  " (SELECT COALESCE(MAX(last_read_id), 0) FROM read_state WHERE peer = ?)");
    query.addBindValue(peer);
    query.addBindValue(peer);
    if (query.exec() && query.next() && !query.value(0).isNull()) {
        return query.value(0).toLongLong();
    }
    return 0;
}

void TranscriptStore::setReaction(
    const QString& peer, const QString& target, const QString& reactor, const QString& emoji)
{
    if (peer.isEmpty() || target.isEmpty() || reactor.isEmpty()) {
        return;
    }
    QSqlQuery query(QSqlDatabase::database(connectionName_));
    if (emoji.isEmpty()) {
        // An empty emoji clears the reactor's reaction on this message.
        query.prepare("DELETE FROM reactions WHERE peer = ? AND target = ? AND reactor = ?");
        query.addBindValue(peer);
        query.addBindValue(target);
        query.addBindValue(reactor);
    } else {
        query.prepare("INSERT INTO reactions (peer, target, reactor, emoji) VALUES (?, ?, ?, ?)"
                      " ON CONFLICT(peer, target, reactor) DO UPDATE SET emoji = excluded.emoji");
        query.addBindValue(peer);
        query.addBindValue(target);
        query.addBindValue(reactor);
        query.addBindValue(emoji);
    }
    if (query.exec()) {
        flush();
    }
}

QVector<Reaction> TranscriptStore::reactionsFor(const QString& peer, const QString& target) const
{
    QVector<Reaction> result;
    if (peer.isEmpty() || target.isEmpty()) {
        return result;
    }
    QSqlQuery query(QSqlDatabase::database(connectionName_));
    query.prepare("SELECT reactor, emoji FROM reactions WHERE peer = ? AND target = ? ORDER BY rowid");
    query.addBindValue(peer);
    query.addBindValue(target);
    if (!query.exec()) {
        return result;
    }
    while (query.next()) {
        result.push_back(Reaction{query.value(0).toString(), query.value(1).toString()});
    }
    return result;
}

}  // namespace bazarish::app
