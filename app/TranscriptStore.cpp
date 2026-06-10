// Bazarish project (c) 2026
#include "TranscriptStore.hpp"

#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

namespace bazarish::app {

TranscriptStore::TranscriptStore() = default;

TranscriptStore::~TranscriptStore()
{
    if (!connectionName_.isEmpty()) {
        QSqlDatabase::database(connectionName_).close();
        QSqlDatabase::removeDatabase(connectionName_);
    }
}

bool TranscriptStore::open(const QString& profileId, const QString& dbPath)
{
    connectionName_ = "transcript-" + profileId;
    QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", connectionName_);
    db.setDatabaseName(dbPath);
    if (!db.open()) {
        return false;
    }
    QSqlQuery query(db);
    return query.exec(
        "CREATE TABLE IF NOT EXISTS messages ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "peer TEXT NOT NULL, outgoing INTEGER, type TEXT, protocolId TEXT, text TEXT,"
        "attName TEXT, attMime TEXT, attSize INTEGER,"
        "attRef TEXT, attKey TEXT, ts INTEGER, status INTEGER)");
}

qint64 TranscriptStore::append(const StoredMessage& message)
{
    QSqlQuery query(QSqlDatabase::database(connectionName_));
    query.prepare(
        "INSERT INTO messages (peer, outgoing, type, protocolId, text, attName, attMime,"
        " attSize, attRef, attKey, ts, status) VALUES (?,?,?,?,?,?,?,?,?,?,?,?)");
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
    query.addBindValue(message.ts);
    query.addBindValue(message.status);
    if (!query.exec()) {
        return 0;
    }
    return query.lastInsertId().toLongLong();
}

void TranscriptStore::updateStatus(qint64 id, int status)
{
    QSqlQuery query(QSqlDatabase::database(connectionName_));
    query.prepare("UPDATE messages SET status = ? WHERE id = ?");
    query.addBindValue(status);
    query.addBindValue(id);
    query.exec();
}

QVector<StoredMessage> TranscriptStore::messagesFor(const QString& peer) const
{
    QVector<StoredMessage> result;
    QSqlQuery query(QSqlDatabase::database(connectionName_));
    query.prepare("SELECT id, peer, outgoing, type, protocolId, text, attName, attMime,"
                  " attSize, attRef, attKey, ts, status FROM messages WHERE peer = ? ORDER BY id");
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
        m.protocolId = query.value(4).toString();
        m.text = query.value(5).toString();
        m.attName = query.value(6).toString();
        m.attMime = query.value(7).toString();
        m.attSize = query.value(8).toLongLong();
        m.attRef = query.value(9).toString();
        m.attKey = query.value(10).toString();
        m.ts = query.value(11).toLongLong();
        m.status = query.value(12).toInt();
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
