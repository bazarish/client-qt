// Bazarish project (c) 2026
#include "TranscriptStore.hpp"

#include "AccountKey.hpp"
#include "SystemNotes.hpp"

#include <bazarish/Bytes.hpp>
#pragma push_macro("emit")
#undef emit
#include <bazarish/Log.hpp>
#pragma pop_macro("emit")

#include <QDateTime>
#include <QFileInfo>
#include <QStorageInfo>
#include <QStringList>

#include <sqlcipher/sqlite3.h>

#include <algorithm>
#include <stdexcept>
#include <string>

namespace bazarish::app {

namespace {

constexpr qint64 kMsPerSecond = 1000;

// What the auto-delete timer takes away: what people wrote and sent, never a service note.
const QStringList kExpiringTypes{QStringLiteral("text"), QStringLiteral("image"),
    QStringLiteral("audio"), QStringLiteral("file")};

QString expiringTypesSql()
{
    QStringList quoted;
    for (const QString& type : kExpiringTypes) {
        quoted << QLatin1Char('\'') + type + QLatin1Char('\'');
    }
    return quoted.join(QLatin1Char(','));
}

class Value {
public:
    Value(sqlite3_stmt* const stmt, const int column)
        : stmt_(stmt)
        , column_(column)
    {
    }

    QString toString() const
    {
        const unsigned char* const text = sqlite3_column_text(stmt_, column_);
        return text == nullptr ? QString() : QString::fromUtf8(reinterpret_cast<const char*>(text));
    }
    qint64 toLongLong() const { return sqlite3_column_int64(stmt_, column_); }
    int toInt() const { return sqlite3_column_int(stmt_, column_); }
    bool isNull() const { return sqlite3_column_type(stmt_, column_) == SQLITE_NULL; }
    QByteArray toByteArray() const
    {
        const void* const blob = sqlite3_column_blob(stmt_, column_);
        const int size = sqlite3_column_bytes(stmt_, column_);
        return blob == nullptr ? QByteArray()
                               : QByteArray(static_cast<const char*>(blob), size);
    }

private:
    sqlite3_stmt* stmt_ = nullptr;
    int column_ = 0;
};

class Query {
public:
    explicit Query(sqlite3* const db)
        : db_(db)
    {
    }
    ~Query() { sqlite3_finalize(stmt_); }

    Query(const Query&) = delete;
    Query& operator=(const Query&) = delete;

    bool prepare(const QString& sql)
    {
        sqlite3_finalize(stmt_);
        stmt_ = nullptr;
        bound_ = 0;
        const QByteArray text = sql.toUtf8();
        if (sqlite3_prepare_v2(db_, text.constData(), text.size(), &stmt_, nullptr) != SQLITE_OK) {
            bazarish::log::warn("transcript: {}", sqlite3_errmsg(db_));
            return false;
        }
        return true;
    }

    void addBindValue(const QString& value)
    {
        const QByteArray text = value.toUtf8();
        sqlite3_bind_text(stmt_, ++bound_, text.constData(), text.size(), SQLITE_TRANSIENT);
    }
    void addBindValue(const char* const value) { addBindValue(QString::fromUtf8(value)); }
    void addBindValue(const qint64 value) { sqlite3_bind_int64(stmt_, ++bound_, value); }
    void addBindValue(const int value) { sqlite3_bind_int(stmt_, ++bound_, value); }

    bool exec()
    {
        const int status = sqlite3_step(stmt_);
        if (status != SQLITE_DONE && status != SQLITE_ROW) {
            bazarish::log::warn("transcript: {}", sqlite3_errmsg(db_));
            return false;
        }
        changes_ = sqlite3_changes(db_);
        stepped_ = status == SQLITE_ROW;
        return true;
    }

    bool exec(const QString& sql) { return prepare(sql) && exec(); }

    bool next()
    {
        if (stepped_) {
            stepped_ = false;
            return true;
        }
        return sqlite3_step(stmt_) == SQLITE_ROW;
    }

    Value value(const int column) const { return Value(stmt_, column); }
    qint64 lastInsertId() const { return sqlite3_last_insert_rowid(db_); }
    int numRowsAffected() const { return changes_; }

private:
    sqlite3* db_ = nullptr;
    sqlite3_stmt* stmt_ = nullptr;
    int bound_ = 0;
    int changes_ = 0;
    bool stepped_ = false;
};

class Transaction {
public:
    explicit Transaction(sqlite3* const db)
        : db_(db)
    {
        Query begin(db_);
        begun_ = begin.exec("BEGIN IMMEDIATE");
    }
    ~Transaction()
    {
        if (!begun_ || committed_) {
            return;
        }
        Query rollback(db_);
        if (!rollback.exec("ROLLBACK")) {
            bazarish::log::warn("transcript: a change could not be rolled back");
        }
    }

    Transaction(const Transaction&) = delete;
    Transaction& operator=(const Transaction&) = delete;

    bool begun() const { return begun_; }

    bool commit()
    {
        Query end(db_);
        committed_ = end.exec("COMMIT");
        return committed_;
    }

private:
    sqlite3* db_ = nullptr;
    bool begun_ = false;
    bool committed_ = false;
};

const char* const kMessageColumns = "id, peer, outgoing, type, e2eId, text, attName,"
                                    " attMime, attSize, attRef, attSrcPath, keyboard,"
                                    " edited, ts, status, orderKey, savedPath, replyTo,"
                                    " hasPicture, attDurationMs, attWave, forwarded,"
                                    " (SELECT author FROM group_posts g"
                                    " WHERE g.peer = messages.peer AND g.e2eId = messages.e2eId),"
                                    " (SELECT contactable FROM group_posts g"
                                    " WHERE g.peer = messages.peer AND g.e2eId = messages.e2eId)";

void sortByOrder(QVector<StoredMessage>& rows)
{
    std::sort(rows.begin(), rows.end(), [](const StoredMessage& a, const StoredMessage& b) {
        if (a.orderKey != b.orderKey) {
            return a.orderKey < b.orderKey;
        }
        return a.id < b.id;
    });
}

StoredMessage readMessageRow(const Query& query)
{
    StoredMessage m;
    m.id = query.value(0).toLongLong();
    m.peer = query.value(1).toString();
    m.outgoing = query.value(2).toInt() != 0;
    m.type = query.value(3).toString();
    m.e2eId = query.value(4).toString();
    m.text = query.value(5).toString();
    m.attName = query.value(6).toString();
    m.attMime = query.value(7).toString();
    m.attSize = query.value(8).toLongLong();
    m.attRef = query.value(9).toString();
    m.attSrcPath = query.value(10).toString();
    m.keyboard = query.value(11).toString();
    m.edited = query.value(12).toInt() != 0;
    m.ts = query.value(13).toLongLong();
    m.status = query.value(14).toInt();
    m.orderKey = query.value(15).toLongLong();
    m.savedPath = query.value(16).toString();
    m.replyTo = query.value(17).toString();
    m.hasPicture = query.value(18).toInt() != 0;
    m.attDurationMs = query.value(19).toLongLong();
    m.attWave = query.value(20).toString();
    m.forwarded = query.value(21).toInt() != 0;
    m.author = query.value(22).toString();
    m.contactable = query.value(23).toInt() != 0;
    const bool groupPost = !query.value(22).isNull();
    m.fromHost = groupPost && m.author.isEmpty();
    return m;
}

}  // namespace

TranscriptReader::TranscriptReader() = default;

TranscriptReader::~TranscriptReader()
{
    close();
}

void TranscriptReader::close()
{
    sqlite3_close(db_);
    db_ = nullptr;
    path_.clear();
}

namespace {

sqlite3* openKeyed(const QString& path, const Bytes& key)
{
    sqlite3* db = nullptr;
    if (sqlite3_open(path.toUtf8().constData(), &db) != SQLITE_OK) {
        sqlite3_close(db);
        return nullptr;
    }
    sqlite3_exec(db, "PRAGMA cipher_log_level = NONE", nullptr, nullptr, nullptr);
    constexpr int kBusyTimeoutMs = 5000;
    sqlite3_busy_timeout(db, kBusyTimeoutMs);
    const std::string pragma = "PRAGMA key = \"x'" + toHex(key) + "'\"";
    if (sqlite3_exec(db, pragma.c_str(), nullptr, nullptr, nullptr) != SQLITE_OK) {
        sqlite3_close(db);
        return nullptr;
    }
    return db;
}

bool readable(sqlite3* const db)
{
    return sqlite3_exec(db, "SELECT count(*) FROM sqlite_master", nullptr, nullptr, nullptr)
        == SQLITE_OK;
}

// One process writes while another reads, and a reader must never hold the writer up.
bool takesWriteAheadLog(sqlite3* const db)
{
    Query journal(db);
    return journal.exec("PRAGMA journal_mode = WAL")
        && journal.value(0).toString() == QStringLiteral("wal");
}

}  // namespace

bool TranscriptReader::open(const QString& dbPath, const Bytes& key)
{
    db_ = openKeyed(dbPath, key);
    if (db_ == nullptr) {
        return false;
    }
    if (!readable(db_)) {
        close();
        return false;
    }
    path_ = dbPath;
    return true;
}

bool TranscriptStore::open(const QString& accountId, const QString& dbPath, const QString& passphrase)
{
    (void)accountId;
    Bytes key;
    try {
        key = client::accountkey::keyFor(dbPath.toStdString(), passphrase.toStdString());
    } catch (const std::exception& error) {
        bazarish::log::warn("transcript: {}", error.what());
        return false;
    }
    db_ = openKeyed(dbPath, key);
    if (db_ == nullptr) {
        return false;
    }
    if (!readable(db_)) {
        close();
        return false;
    }
    path_ = dbPath;

    if (!takesWriteAheadLog(db_)) {
        bazarish::log::warn("transcript: the database did not take a write-ahead log");
        close();
        return false;
    }

    Query query(db_);
    if (!query.exec(
            "CREATE TABLE IF NOT EXISTS messages ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "peer TEXT NOT NULL, outgoing INTEGER, type TEXT, e2eId TEXT,"
            "text TEXT, attName TEXT, attMime TEXT, attSize INTEGER,"
            "attRef TEXT, attSrcPath TEXT, keyboard TEXT, edited INTEGER,"
            " ts INTEGER, status INTEGER, orderKey INTEGER, savedPath TEXT,"
            " replyTo TEXT, hasPicture INTEGER,"
            " attDurationMs INTEGER, attWave TEXT, forwarded INTEGER)")) {
        return false;
    }

    if (!query.exec("CREATE TABLE IF NOT EXISTS read_state ("
                    "peer TEXT PRIMARY KEY, last_read_id INTEGER NOT NULL)")) {
        return false;
    }
    if (!query.exec("CREATE TABLE IF NOT EXISTS reactions ("
                    "peer TEXT, target TEXT, reactor TEXT, emoji TEXT,"
                    " PRIMARY KEY (peer, target, reactor))")) {
        return false;
    }
    if (!query.exec("CREATE TABLE IF NOT EXISTS pinned_chats (peer TEXT PRIMARY KEY)")) {
        return false;
    }
    if (!query.exec("CREATE TABLE IF NOT EXISTS group_posts ("
                    "peer TEXT, e2eId TEXT, author TEXT, contactable INTEGER,"
                    " PRIMARY KEY (peer, e2eId))")) {
        return false;
    }
    if (!query.exec("CREATE TABLE IF NOT EXISTS group_reaction_counts ("
                    "peer TEXT, target TEXT, emoji TEXT, count INTEGER,"
                    " PRIMARY KEY (peer, target, emoji))")) {
        return false;
    }
    if (!query.exec("CREATE TABLE IF NOT EXISTS group_state ("
                    "peer TEXT PRIMARY KEY, kind TEXT, members INTEGER, write INTEGER,"
                    " admin INTEGER, at INTEGER)")) {
        return false;
    }
    if (!query.exec("CREATE TABLE IF NOT EXISTS chat_timers ("
                    "peer TEXT PRIMARY KEY, seconds INTEGER NOT NULL)")) {
        return false;
    }
    if (!query.exec("CREATE TABLE IF NOT EXISTS expiries ("
                    "id INTEGER PRIMARY KEY, peer TEXT NOT NULL, at INTEGER NOT NULL)")) {
        return false;
    }
    if (!query.exec("CREATE INDEX IF NOT EXISTS expiries_by_at ON expiries (at)")) {
        return false;
    }
    if (!query.exec("CREATE UNIQUE INDEX IF NOT EXISTS messages_by_e2e"
                    " ON messages (peer, e2eId, outgoing) WHERE e2eId != ''")) {
        return false;
    }
    return query.exec("CREATE INDEX IF NOT EXISTS messages_by_peer_id ON messages (peer, id)");
}

qint64 TranscriptStore::append(const StoredMessage& message)
{
    Transaction change(db_);
    if (!change.begun()) {
        failed();
        return 0;
    }
    if (!message.e2eId.isEmpty()) {
        if (const qint64 existing = idForKey(message.peer, message.e2eId, message.outgoing);
            existing != 0) {
            bazarish::log::info("transcript: {} was offered again; the row it already has stands",
                message.e2eId.toStdString());
            return existing;
        }
    }
    Query query(db_);
    query.prepare(
        "INSERT INTO messages (peer, outgoing, type, e2eId, text, attName, attMime,"
        " attSize, attRef, attSrcPath, keyboard, edited, ts, status, orderKey, replyTo,"
        " attDurationMs, attWave, forwarded)"
        " VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)");
    query.addBindValue(message.peer);
    query.addBindValue(message.outgoing ? 1 : 0);
    query.addBindValue(message.type);
    query.addBindValue(message.e2eId);
    query.addBindValue(message.text);
    query.addBindValue(message.attName);
    query.addBindValue(message.attMime);
    query.addBindValue(message.attSize);
    query.addBindValue(message.attRef);
    query.addBindValue(message.attSrcPath);
    query.addBindValue(message.keyboard);
    query.addBindValue(message.edited ? 1 : 0);
    query.addBindValue(message.ts);
    query.addBindValue(message.status);
    query.addBindValue(message.orderKey);
    query.addBindValue(message.replyTo);
    query.addBindValue(message.attDurationMs);
    query.addBindValue(message.attWave);
    query.addBindValue(message.forwarded ? 1 : 0);
    if (!query.exec()) {
        failed();
        return 0;
    }
    const qint64 id = query.lastInsertId();
    if (message.outgoing && kExpiringTypes.contains(message.type)) {
        const qint64 seconds = chatTimer(message.peer);
        if (seconds > 0 && !stampExpiry(id, message.peer, message.ts + seconds * kMsPerSecond)) {
            return 0;
        }
    }
    if (!message.author.isEmpty() || message.fromHost) {
        Query post(db_);
        post.prepare("INSERT OR REPLACE INTO group_posts (peer, e2eId, author, contactable)"
                     " VALUES (?, ?, ?, ?)");
        post.addBindValue(message.peer);
        post.addBindValue(message.e2eId);
        post.addBindValue(message.author);
        post.addBindValue(message.contactable ? 1 : 0);
        if (!post.exec()) {
            failed();
            return 0;
        }
    }
    if (!change.commit()) {
        failed();
        return 0;
    }
    return id;
}

qint64 TranscriptReader::idForKey(
    const QString& peer, const QString& e2eId, const bool outgoing) const
{
    if (e2eId.isEmpty()) {
        return 0;
    }
    Query query(db_);
    query.prepare("SELECT id FROM messages WHERE peer = ? AND e2eId = ? AND outgoing = ? LIMIT 1");
    query.addBindValue(peer);
    query.addBindValue(e2eId);
    query.addBindValue(outgoing ? 1 : 0);
    if (!query.exec() || !query.next()) {
        return 0;
    }
    return query.value(0).toLongLong();
}

bool TranscriptStore::updateStatus(qint64 id, int status)
{
    Query query(db_);
    query.prepare("UPDATE messages SET status = ? WHERE id = ?");
    query.addBindValue(status);
    query.addBindValue(id);
    if (!query.exec()) {
        return failed();
    }
    return true;
}

QVector<StoredMessage> TranscriptReader::messagesFor(const QString& peer) const
{
    QVector<StoredMessage> result;
    Query query(db_);
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

QVector<StoredMessage> TranscriptReader::latestMessages(const QString& peer, int limit) const
{
    QVector<StoredMessage> result;
    Query query(db_);
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

QVector<StoredMessage> TranscriptReader::olderMessages(
    const QString& peer, qint64 beforeId, int limit) const
{
    QVector<StoredMessage> result;
    Query query(db_);
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

StoredMessage TranscriptReader::nextVoiceAfter(const QString& peer, const qint64 afterId) const
{
    StoredMessage found;
    Query query(db_);
    query.prepare(QStringLiteral("SELECT %1 FROM messages WHERE peer = ? AND id > ?"
                                 " AND type = 'audio' ORDER BY id ASC LIMIT 1")
                      .arg(kMessageColumns));
    query.addBindValue(peer);
    query.addBindValue(afterId);
    if (!query.exec() || !query.next()) {
        return found;
    }
    return readMessageRow(query);
}

QVector<StoredMessage> TranscriptReader::newerMessages(
    const QString& peer, qint64 afterId, int limit) const
{
    QVector<StoredMessage> result;
    Query query(db_);
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

bool TranscriptReader::hasMessagesBefore(const QString& peer, qint64 id) const
{
    Query query(db_);
    query.prepare("SELECT 1 FROM messages WHERE peer = ? AND id < ? LIMIT 1");
    query.addBindValue(peer);
    query.addBindValue(id);
    return query.exec() && query.next();
}

bool TranscriptReader::hasMessagesAfter(const QString& peer, qint64 id) const
{
    Query query(db_);
    query.prepare("SELECT 1 FROM messages WHERE peer = ? AND id > ? LIMIT 1");
    query.addBindValue(peer);
    query.addBindValue(id);
    return query.exec() && query.next();
}

QVector<SearchHit> TranscriptReader::searchInPeer(const QString& peer, const QString& query) const
{
    QVector<SearchHit> hits;
    if (query.isEmpty()) {
        return hits;
    }
    Query sql(db_);
    sql.prepare("SELECT id, ts, text, outgoing, attName, type FROM messages"
                " WHERE peer = ? AND (text <> '' OR attName <> '') ORDER BY id DESC");
    sql.addBindValue(peer);
    if (!sql.exec()) {
        return hits;
    }
    constexpr int kMaxHits = 500;
    while (sql.next() && hits.size() < kMaxHits) {
        const QString stored = sql.value(2).toString();
        const QString text = isServiceMessage(sql.value(5).toString())
            ? systemNoteText(stored)
            : stored;
        const QString attName = sql.value(4).toString();
        if (!text.contains(query, Qt::CaseInsensitive)
            && !attName.contains(query, Qt::CaseInsensitive)) {
            continue;
        }
        SearchHit hit;
        hit.id = sql.value(0).toLongLong();
        hit.ts = sql.value(1).toLongLong();
        hit.text = !text.isEmpty() ? text : (QStringLiteral("📎 ") + attName);
        hit.outgoing = sql.value(3).toInt() != 0;
        hits.push_back(hit);
    }
    return hits;
}

std::optional<int> TranscriptStore::failUnsentOnLoad(
    const int preparingStatus, const int deliveringStatus, const int failedStatus)
{
    Query query(db_);
    query.prepare(
        "UPDATE messages SET status = ? WHERE outgoing = 1 AND status IN (?, ?)");
    query.addBindValue(failedStatus);
    query.addBindValue(preparingStatus);
    query.addBindValue(deliveringStatus);
    if (!query.exec()) {
        failed();
        return std::nullopt;
    }
    return query.numRowsAffected();
}

std::optional<int> TranscriptStore::settleUnfinishedNotes(const QString& type,
    const int preparingStatus, const int settledStatus, const QString& text)
{
    Query query(db_);
    query.prepare("UPDATE messages SET status = ?, text = ? WHERE type = ? AND status = ?");
    query.addBindValue(settledStatus);
    query.addBindValue(text);
    query.addBindValue(type);
    query.addBindValue(preparingStatus);
    if (!query.exec()) {
        failed();
        return std::nullopt;
    }
    return query.numRowsAffected();
}

bool TranscriptStore::markOutgoingReadUpTo(
    const QString& peer, qint64 uptoId, int readStatus, int minStatus, int maxStatus)
{
    Query query(db_);
    query.prepare("UPDATE messages SET status = ? WHERE outgoing = 1 AND peer = ? AND id <= ?"
                  " AND status >= ? AND status <= ?");
    query.addBindValue(readStatus);
    query.addBindValue(peer);
    query.addBindValue(uptoId);
    query.addBindValue(minStatus);
    query.addBindValue(maxStatus);
    if (!query.exec()) {
        return failed();
    }
    return true;
}

QString TranscriptReader::sourcePathFor(qint64 id) const
{
    Query query(db_);
    query.prepare("SELECT attSrcPath FROM messages WHERE id = ? LIMIT 1");
    query.addBindValue(id);
    if (query.exec() && query.next()) {
        return query.value(0).toString();
    }
    return {};
}

bool TranscriptStore::setSavedPath(qint64 id, const QString& path)
{
    Query query(db_);
    query.prepare("UPDATE messages SET savedPath = ? WHERE id = ?");
    query.addBindValue(path);
    query.addBindValue(id);
    if (!query.exec()) {
        return failed();
    }
    return true;
}

QByteArray TranscriptReader::media(const QString& key) const
{
    Query query(db_);
    if (!query.prepare("SELECT value FROM state WHERE name = ?")) {
        return {};
    }
    query.addBindValue(key);
    if (!query.exec() || !query.next()) {
        return {};
    }
    return query.value(0).toByteArray();
}

bool TranscriptStore::setHasPicture(const qint64 id, const bool has)
{
    Query query(db_);
    query.prepare("UPDATE messages SET hasPicture = ? WHERE id = ?");
    query.addBindValue(has ? 1 : 0);
    query.addBindValue(id);
    if (!query.exec()) {
        return failed();
    }
    return true;
}

qint64 TranscriptReader::idForE2e(const QString& e2eId) const
{
    if (e2eId.isEmpty()) {
        return 0;
    }
    Query query(db_);
    query.prepare("SELECT id FROM messages WHERE e2eId = ? AND outgoing = 1 LIMIT 1");
    query.addBindValue(e2eId);
    if (query.exec() && query.next()) {
        return query.value(0).toLongLong();
    }
    return 0;
}

qint64 TranscriptReader::idForIncomingE2e(const QString& e2eId, const QString& peer) const
{
    if (e2eId.isEmpty()) {
        return 0;
    }
    Query query(db_);
    query.prepare(
        "SELECT id FROM messages WHERE e2eId = ? AND peer = ? AND outgoing = 0 LIMIT 1");
    query.addBindValue(e2eId);
    query.addBindValue(peer);
    if (query.exec() && query.next()) {
        return query.value(0).toLongLong();
    }
    return 0;
}

qint64 TranscriptReader::oldestOfType(
    const QString& peer, const QString& type, const bool outgoing) const
{
    Query query(db_);
    query.prepare("SELECT id FROM messages WHERE peer = ? AND type = ? AND outgoing = ?"
                  " ORDER BY id LIMIT 1");
    query.addBindValue(peer);
    query.addBindValue(type);
    query.addBindValue(outgoing ? 1 : 0);
    if (query.exec() && query.next()) {
        return query.value(0).toLongLong();
    }
    return 0;
}

qint64 TranscriptReader::idForAnyProtocol(const QString& e2eId, const QString& peer) const
{
    if (e2eId.isEmpty()) {
        return 0;
    }
    Query query(db_);
    query.prepare("SELECT id FROM messages WHERE e2eId = ? AND peer = ? ORDER BY id LIMIT 1");
    query.addBindValue(e2eId);
    query.addBindValue(peer);
    if (query.exec() && query.next()) {
        return query.value(0).toLongLong();
    }
    return 0;
}

StoredMessage TranscriptReader::messageById(const qint64 id) const
{
    StoredMessage m;
    Query query(db_);
    query.prepare(QStringLiteral("SELECT %1 FROM messages WHERE id = ?").arg(kMessageColumns));
    query.addBindValue(id);
    if (query.exec() && query.next()) {
        m = readMessageRow(query);
    }
    return m;
}

StoredMessage TranscriptReader::messageByE2e(
    const QString& e2eId, const QString& peer) const
{
    StoredMessage m;
    if (e2eId.isEmpty()) {
        return m;
    }
    Query query(db_);
    query.prepare(QStringLiteral("SELECT %1 FROM messages WHERE e2eId = ? AND peer = ?"
                                 " ORDER BY id LIMIT 1")
                      .arg(kMessageColumns));
    query.addBindValue(e2eId);
    query.addBindValue(peer);
    if (query.exec() && query.next()) {
        m = readMessageRow(query);
    }
    return m;
}

bool TranscriptStore::editContent(qint64 id, const QString& text, const QString& keyboard)
{
    Query query(db_);
    query.prepare("UPDATE messages SET text = ?, keyboard = ?, edited = 1 WHERE id = ?");
    query.addBindValue(text);
    query.addBindValue(keyboard);
    query.addBindValue(id);
    if (!query.exec()) {
        return failed();
    }
    return true;
}

bool TranscriptStore::setType(qint64 id, const QString& type)
{
    Query query(db_);
    query.prepare("UPDATE messages SET type = ? WHERE id = ?");
    query.addBindValue(type);
    query.addBindValue(id);
    if (!query.exec()) {
        return failed();
    }
    return true;
}

bool TranscriptStore::removeById(qint64 id)
{
    Transaction change(db_);
    if (!change.begun()) {
        return failed();
    }
    QString named;
    Query naming(db_);
    naming.prepare("SELECT e2eId FROM messages WHERE id = ?");
    naming.addBindValue(id);
    if (!naming.exec()) {
        return failed();
    }
    if (naming.next()) {
        named = naming.value(0).toString();
    }
    if (!removeMediaOfMessage(named)) {
        return false;
    }
    if (!named.isEmpty()) {
        for (const char* statement : {"DELETE FROM reactions WHERE target = ?",
                 "DELETE FROM group_reaction_counts WHERE target = ?",
                 "DELETE FROM group_posts WHERE e2eId = ?"}) {
            Query side(db_);
            side.prepare(QString::fromUtf8(statement));
            side.addBindValue(named);
            if (!side.exec()) {
                return failed();
            }
        }
    }
    for (const char* statement :
        {"DELETE FROM expiries WHERE id = ?", "DELETE FROM messages WHERE id = ?"}) {
        Query query(db_);
        query.prepare(QString::fromUtf8(statement));
        query.addBindValue(id);
        if (!query.exec()) {
            return failed();
        }
    }
    if (!change.commit()) {
        return failed();
    }
    return true;
}

bool TranscriptStore::clearPeerRows(const QString& peer)
{
    if (!removeMediaOfPeer(peer)) {
        return false;
    }
    for (const char* statement : {"DELETE FROM messages WHERE peer = ?",
             "DELETE FROM expiries WHERE peer = ?",
             "DELETE FROM reactions WHERE peer = ?", "DELETE FROM read_state WHERE peer = ?",
             "DELETE FROM group_posts WHERE peer = ?",
             "DELETE FROM group_reaction_counts WHERE peer = ?"}) {
        Query query(db_);
        query.prepare(QString::fromUtf8(statement));
        query.addBindValue(peer);
        if (!query.exec()) {
            return failed();
        }
    }
    return true;
}

bool TranscriptStore::clearPeer(const QString& peer)
{
    Transaction change(db_);
    if (!change.begun()) {
        return failed();
    }
    if (!clearPeerRows(peer)) {
        return false;
    }
    if (!change.commit()) {
        return failed();
    }
    return true;
}

bool TranscriptStore::forgetPeer(const QString& peer)
{
    Transaction change(db_);
    if (!change.begun()) {
        return failed();
    }
    if (!clearPeerRows(peer)) {
        return false;
    }
    for (const char* statement : {"DELETE FROM pinned_chats WHERE peer = ?",
             "DELETE FROM chat_timers WHERE peer = ?",
             "DELETE FROM group_state WHERE peer = ?"}) {
        Query query(db_);
        query.prepare(QString::fromUtf8(statement));
        query.addBindValue(peer);
        if (!query.exec()) {
            return failed();
        }
    }
    if (!change.commit()) {
        return failed();
    }
    return true;
}

bool TranscriptStore::setGroupState(const QString& peer, const GroupState& state)
{
    Query query(db_);
    query.prepare("INSERT OR REPLACE INTO group_state (peer, kind, members, write, admin, at)"
                  " VALUES (?, ?, ?, ?, ?, ?)");
    query.addBindValue(peer);
    query.addBindValue(state.kind);
    query.addBindValue(state.members);
    query.addBindValue(state.write ? 1 : 0);
    query.addBindValue(state.admin ? 1 : 0);
    query.addBindValue(state.at);
    if (!query.exec()) {
        return failed();
    }
    return true;
}

std::optional<GroupState> TranscriptReader::groupState(const QString& peer) const
{
    Query query(db_);
    query.prepare("SELECT kind, members, write, admin, at FROM group_state WHERE peer = ?");
    query.addBindValue(peer);
    if (!query.exec() || !query.next()) {
        return std::nullopt;
    }
    return GroupState{query.value(0).toString(), query.value(1).toLongLong(),
        query.value(2).toInt() != 0, query.value(3).toInt() != 0, query.value(4).toLongLong()};
}

bool TranscriptStore::dropGroupState(const QString& peer)
{
    Query query(db_);
    query.prepare("DELETE FROM group_state WHERE peer = ?");
    query.addBindValue(peer);
    if (!query.exec()) {
        return failed();
    }
    return true;
}

bool TranscriptStore::setGroupReactionCounts(
    const QString& peer, const QString& target, const QMap<QString, qint64>& counts)
{
    Transaction change(db_);
    if (!change.begun()) {
        return failed();
    }
    Query clear(db_);
    clear.prepare("DELETE FROM group_reaction_counts WHERE peer = ? AND target = ?");
    clear.addBindValue(peer);
    clear.addBindValue(target);
    if (!clear.exec()) {
        return failed();
    }
    for (auto it = counts.cbegin(); it != counts.cend(); ++it) {
        Query insert(db_);
        insert.prepare("INSERT INTO group_reaction_counts (peer, target, emoji, count)"
                       " VALUES (?, ?, ?, ?)");
        insert.addBindValue(peer);
        insert.addBindValue(target);
        insert.addBindValue(it.key());
        insert.addBindValue(it.value());
        if (!insert.exec()) {
            return failed();
        }
    }
    if (!change.commit()) {
        return failed();
    }
    return true;
}

QMap<QString, qint64> TranscriptReader::groupReactionCounts(
    const QString& peer, const QString& target) const
{
    QMap<QString, qint64> counts;
    Query query(db_);
    query.prepare("SELECT emoji, count FROM group_reaction_counts WHERE peer = ? AND target = ?");
    query.addBindValue(peer);
    query.addBindValue(target);
    if (!query.exec()) {
        return counts;
    }
    while (query.next()) {
        counts.insert(query.value(0).toString(), query.value(1).toLongLong());
    }
    return counts;
}

QStringList TranscriptReader::conversationPeers() const
{
    QStringList peers;
    Query query(db_);
    if (query.exec("SELECT DISTINCT peer FROM messages")) {
        while (query.next()) {
            peers << query.value(0).toString();
        }
    }
    return peers;
}

TranscriptReader::LastMessage TranscriptReader::lastMessage(const QString& peer) const
{
    Query query(db_);
    query.prepare("SELECT text, type, attName, (SELECT author FROM group_posts g"
                  " WHERE g.peer = messages.peer AND g.e2eId = messages.e2eId)"
                  " FROM messages WHERE peer = ?"
                  " AND (text != '' OR type IN ('file','image','audio'))"
                  " ORDER BY orderKey DESC, id DESC LIMIT 1");
    query.addBindValue(peer);
    if (query.exec() && query.next()) {
        const QString text = query.value(0).toString();
        const QString author = query.value(3).toString();
        if (!text.isEmpty()) {
            return {text, query.value(1).toString(), {}, author};
        }
        return {{}, query.value(1).toString(), query.value(2).toString(), author};
    }
    return {};
}

qint64 TranscriptReader::lastTime(const QString& peer) const
{
    Query query(db_);
    query.prepare(
        "SELECT ts FROM messages WHERE peer = ? ORDER BY orderKey DESC, id DESC LIMIT 1");
    query.addBindValue(peer);
    if (query.exec() && query.next()) {
        return query.value(0).toLongLong();
    }
    return 0;
}

bool TranscriptStore::setLastReadId(const QString& peer, qint64 id)
{
    if (peer.isEmpty() || id <= 0) {
        return true;
    }
    Transaction change(db_);
    if (!change.begun()) {
        return failed();
    }
    const qint64 before = lastReadId(peer);
    Query query(db_);
    query.prepare("INSERT OR REPLACE INTO read_state (peer, last_read_id) VALUES (?,"
                  " max(?, coalesce((SELECT last_read_id FROM read_state WHERE peer = ?), 0)))");
    query.addBindValue(peer);
    query.addBindValue(id);
    query.addBindValue(peer);
    if (!query.exec() || !stampNewlyRead(peer, before, lastReadId(peer)) || !change.commit()) {
        return failed();
    }
    return true;
}

bool TranscriptStore::applyReadThrough(const QString& peer, const qint64 sentAtMs)
{
    if (peer.isEmpty() || sentAtMs <= 0) {
        return true;
    }
    Transaction change(db_);
    if (!change.begun()) {
        return failed();
    }
    const qint64 before = lastReadId(peer);
    Query query(db_);
    query.prepare("INSERT OR REPLACE INTO read_state (peer, last_read_id) VALUES (?,"
                  " max(coalesce((SELECT last_read_id FROM read_state WHERE peer = ?), 0),"
                  " coalesce((SELECT MAX(id) FROM messages WHERE peer = ? AND outgoing = 0"
                  " AND type != 'system' AND ts <= ?), 0)))");
    query.addBindValue(peer);
    query.addBindValue(peer);
    query.addBindValue(peer);
    query.addBindValue(sentAtMs);
    if (!query.exec() || !stampNewlyRead(peer, before, lastReadId(peer)) || !change.commit()) {
        return failed();
    }
    return true;
}

bool TranscriptStore::stampExpiry(const qint64 id, const QString& peer, const qint64 atMs)
{
    Query query(db_);
    query.prepare("INSERT OR IGNORE INTO expiries (id, peer, at) VALUES (?, ?, ?)");
    query.addBindValue(id);
    query.addBindValue(peer);
    query.addBindValue(atMs);
    return query.exec() || failed();
}

bool TranscriptStore::stampNewlyRead(const QString& peer, const qint64 afterId, const qint64 uptoId)
{
    const qint64 seconds = chatTimer(peer);
    if (seconds <= 0 || uptoId <= afterId) {
        return true;
    }
    Query query(db_);
    query.prepare(QStringLiteral("INSERT OR IGNORE INTO expiries (id, peer, at)"
                                 " SELECT id, peer, ? FROM messages WHERE peer = ? AND outgoing = 0"
                                 " AND id > ? AND id <= ? AND type IN (%1)")
                      .arg(expiringTypesSql()));
    query.addBindValue(QDateTime::currentMSecsSinceEpoch() + seconds * kMsPerSecond);
    query.addBindValue(peer);
    query.addBindValue(afterId);
    query.addBindValue(uptoId);
    return query.exec() || failed();
}

qint64 TranscriptReader::chatTimer(const QString& peer) const
{
    Query query(db_);
    query.prepare("SELECT seconds FROM chat_timers WHERE peer = ? LIMIT 1");
    query.addBindValue(peer);
    if (query.exec() && query.next()) {
        return query.value(0).toLongLong();
    }
    return 0;
}

bool TranscriptStore::setChatTimer(const QString& peer, const qint64 seconds)
{
    Query query(db_);
    if (seconds > 0) {
        query.prepare("INSERT OR REPLACE INTO chat_timers (peer, seconds) VALUES (?, ?)");
        query.addBindValue(peer);
        query.addBindValue(seconds);
    } else {
        query.prepare("DELETE FROM chat_timers WHERE peer = ?");
        query.addBindValue(peer);
    }
    return query.exec() || failed();
}

QVector<QPair<QString, qint64>> TranscriptReader::expiredBy(const qint64 nowMs) const
{
    QVector<QPair<QString, qint64>> due;
    Query query(db_);
    query.prepare("SELECT peer, id FROM expiries WHERE at <= ? ORDER BY at");
    query.addBindValue(nowMs);
    if (query.exec()) {
        while (query.next()) {
            due.append({query.value(0).toString(), query.value(1).toLongLong()});
        }
    }
    return due;
}

qint64 TranscriptReader::lastReadId(const QString& peer) const
{
    Query query(db_);
    query.prepare("SELECT last_read_id FROM read_state WHERE peer = ? LIMIT 1");
    query.addBindValue(peer);
    if (query.exec() && query.next()) {
        return query.value(0).toLongLong();
    }
    return 0;
}

bool TranscriptStore::setPinned(const QString& peer, bool pinned)
{
    if (peer.isEmpty()) {
        return true;
    }
    Query query(db_);
    if (pinned) {
        query.prepare("INSERT OR IGNORE INTO pinned_chats (peer) VALUES (?)");
    } else {
        query.prepare("DELETE FROM pinned_chats WHERE peer = ?");
    }
    query.addBindValue(peer);
    if (!query.exec()) {
        return failed();
    }
    return true;
}

bool TranscriptReader::isPinned(const QString& peer) const
{
    Query query(db_);
    query.prepare("SELECT 1 FROM pinned_chats WHERE peer = ? LIMIT 1");
    query.addBindValue(peer);
    return query.exec() && query.next();
}

QStringList TranscriptReader::pinnedPeers() const
{
    QStringList peers;
    Query query(db_);
    if (query.exec("SELECT peer FROM pinned_chats")) {
        while (query.next()) {
            peers << query.value(0).toString();
        }
    }
    return peers;
}

int TranscriptReader::unreadCount(const QString& peer) const
{
    Query query(db_);
    query.prepare("SELECT COUNT(*) FROM messages WHERE peer = ? AND outgoing = 0 AND type != 'system' AND id >"
                  " (SELECT COALESCE(MAX(last_read_id), 0) FROM read_state WHERE peer = ?)");
    query.addBindValue(peer);
    query.addBindValue(peer);
    if (query.exec() && query.next()) {
        return query.value(0).toInt();
    }
    return 0;
}

int TranscriptReader::unreadTotal() const
{
    Query query(db_);
    if (query.exec("SELECT COALESCE(SUM((SELECT COUNT(*) FROM messages m WHERE m.peer = p.peer"
                   " AND m.outgoing = 0 AND m.type != 'system' AND m.id > (SELECT"
                   " COALESCE(MAX(last_read_id), 0) FROM read_state r WHERE r.peer = p.peer))), 0)"
                   " FROM (SELECT DISTINCT peer FROM messages) p")
        && query.next()) {
        return query.value(0).toInt();
    }
    return 0;
}

qint64 TranscriptReader::firstUnreadId(const QString& peer) const
{
    Query query(db_);
    query.prepare("SELECT MIN(id) FROM messages WHERE peer = ? AND outgoing = 0 AND type != 'system' AND id >"
                  " (SELECT COALESCE(MAX(last_read_id), 0) FROM read_state WHERE peer = ?)");
    query.addBindValue(peer);
    query.addBindValue(peer);
    if (query.exec() && query.next() && !query.value(0).isNull()) {
        return query.value(0).toLongLong();
    }
    return 0;
}

bool TranscriptStore::setReaction(
    const QString& peer, const QString& target, const QString& reactor, const QString& emoji)
{
    if (peer.isEmpty() || target.isEmpty() || reactor.isEmpty()) {
        return true;
    }
    Query query(db_);
    if (emoji.isEmpty()) {
        query.prepare("DELETE FROM reactions WHERE peer = ? AND target = ? AND reactor = ?");
        query.addBindValue(peer);
        query.addBindValue(target);
        query.addBindValue(reactor);
    } else {
        query.prepare("INSERT OR REPLACE INTO reactions (peer, target, reactor, emoji)"
                      " VALUES (?, ?, ?, ?)");
        query.addBindValue(peer);
        query.addBindValue(target);
        query.addBindValue(reactor);
        query.addBindValue(emoji);
    }
    if (!query.exec()) {
        return failed();
    }
    return true;
}

QVector<Reaction> TranscriptReader::reactionsFor(const QString& peer, const QString& target) const
{
    QVector<Reaction> result;
    if (peer.isEmpty() || target.isEmpty()) {
        return result;
    }
    Query query(db_);
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

namespace {

const char* const kPictureKeyPrefix = "picture:";
const char* const kVoiceKeyPrefix = "voice:";

constexpr int kFixedColumnBytes = 32;

QString messageBytesExpression()
{
    const QStringList columns = {"text", "keyboard", "attWave", "attName", "attMime", "attRef",
        "attSrcPath", "savedPath", "replyTo", "e2eId", "type", "peer"};
    QStringList parts;
    for (const QString& column : columns) {
        parts << QStringLiteral("LENGTH(CAST(COALESCE(m.%1,'') AS BLOB))").arg(column);
    }
    return parts.join(" + ") + QStringLiteral(" + %1").arg(kFixedColumnBytes);
}

QString mediaOfRows(const QString& rowCondition)
{
    return QStringLiteral("DELETE FROM state WHERE name IN ("
                          "SELECT '%1' || e2eId FROM messages WHERE e2eId != '' AND (%3)"
                          " UNION ALL"
                          " SELECT '%2' || e2eId FROM messages WHERE e2eId != '' AND (%3))")
        .arg(QString::fromUtf8(kPictureKeyPrefix), QString::fromUtf8(kVoiceKeyPrefix),
            rowCondition);
}



}  // namespace

bool TranscriptReader::hasMediaTable() const
{
    Query query(db_);
    return query.exec("SELECT 1 FROM sqlite_master WHERE type = 'table' AND name = 'state'")
        && query.next();
}

bool TranscriptStore::removeMediaOfMessage(const QString& e2eId)
{
    if (e2eId.isEmpty() || !hasMediaTable()) {
        return true;
    }
    Query query(db_);
    query.prepare("DELETE FROM state WHERE name = ? OR name = ?");
    query.addBindValue(QString::fromUtf8(kPictureKeyPrefix) + e2eId);
    query.addBindValue(QString::fromUtf8(kVoiceKeyPrefix) + e2eId);
    if (!query.exec()) {
        return failed();
    }
    return true;
}

bool TranscriptStore::failed()
{
    lastError_ = QString::fromUtf8(sqlite3_errmsg(db_));
    return false;
}

bool TranscriptStore::removeMediaOfPeer(const QString& peer)
{
    if (!hasMediaTable()) {
        return true;
    }
    Query query(db_);
    query.prepare(mediaOfRows("peer = ?"));
    query.addBindValue(peer);
    query.addBindValue(peer);
    if (!query.exec()) {
        return failed();
    }
    return true;
}

bool TranscriptStore::removeMediaOfTrimmed(const QString& peer, const int keep)
{
    if (!hasMediaTable()) {
        return true;
    }
    const QString doomed = QStringLiteral(
        "peer = ? AND id NOT IN (SELECT id FROM messages WHERE peer = ? ORDER BY id DESC LIMIT ?)");
    Query query(db_);
    if (!query.prepare(mediaOfRows(doomed))) {
        return false;
    }
    for (int half = 0; half < 2; ++half) {
        query.addBindValue(peer);
        query.addBindValue(peer);
        query.addBindValue(keep);
    }
    return query.exec();
}

QVector<ChatWeight> TranscriptReader::chatWeights() const
{
    QVector<ChatWeight> weights;
    const QString media = hasMediaTable()
        ? QStringLiteral(" LEFT JOIN state AS p ON m.e2eId != '' AND p.name = '%1' || m.e2eId"
                         " LEFT JOIN state AS v ON m.e2eId != '' AND v.name = '%2' || m.e2eId")
              .arg(QString::fromUtf8(kPictureKeyPrefix), QString::fromUtf8(kVoiceKeyPrefix))
        : QString();
    const QString sums = hasMediaTable()
        ? QStringLiteral("TOTAL(COALESCE(LENGTH(p.value), 0) + COALESCE(LENGTH(v.value), 0)),"
                         " TOTAL((p.name IS NOT NULL) + (v.name IS NOT NULL))")
        : QStringLiteral("0, 0");
    Query query(db_);
    if (!query.exec(QStringLiteral("SELECT m.peer, COUNT(*), TOTAL(%1), %2 FROM messages AS m%3"
                                   " GROUP BY m.peer")
                        .arg(messageBytesExpression(), sums, media))) {
        return weights;
    }
    while (query.next()) {
        ChatWeight weight;
        weight.peer = query.value(0).toString();
        weight.messages = query.value(1).toLongLong();
        weight.rowBytes = query.value(2).toLongLong();
        weight.mediaBytes = query.value(3).toLongLong();
        weight.mediaCount = query.value(4).toLongLong();
        weights.append(weight);
    }
    return weights;
}

DatabaseFootprint TranscriptReader::footprint() const
{
    DatabaseFootprint out;
    out.fileBytes = QFileInfo(path_).size() + QFileInfo(path_ + QStringLiteral("-wal")).size();
    qint64 bytesPerPage = 0;
    Query pageSize(db_);
    if (pageSize.exec("PRAGMA page_size") && pageSize.next()) {
        bytesPerPage = pageSize.value(0).toLongLong();
    }
    Query freePages(db_);
    if (bytesPerPage > 0 && freePages.exec("PRAGMA freelist_count") && freePages.next()) {
        out.freeBytes = bytesPerPage * freePages.value(0).toLongLong();
    }
    return out;
}

qint64 TranscriptStore::pruneRowsOf(const QString& peer, const int keep)
{
    if (!removeMediaOfTrimmed(peer, keep)) {
        throw std::runtime_error("the media of trimmed messages could not be removed");
    }
    Query messages(db_);
    if (!messages.prepare("DELETE FROM messages WHERE peer = ? AND id NOT IN ("
                          "SELECT id FROM messages WHERE peer = ? ORDER BY id DESC LIMIT ?)")) {
        throw std::runtime_error("the conversation could not be trimmed");
    }
    messages.addBindValue(peer);
    messages.addBindValue(peer);
    messages.addBindValue(keep);
    if (!messages.exec()) {
        throw std::runtime_error("the conversation could not be trimmed");
    }
    const qint64 removed = messages.numRowsAffected();
    Query expiries(db_);
    if (!expiries.prepare("DELETE FROM expiries WHERE peer = ? AND id NOT IN ("
                          "SELECT id FROM messages WHERE peer = ?)")) {
        throw std::runtime_error("the timers of trimmed messages could not be removed");
    }
    expiries.addBindValue(peer);
    expiries.addBindValue(peer);
    if (!expiries.exec()) {
        throw std::runtime_error("the timers of trimmed messages could not be removed");
    }
    Query reactions(db_);
    if (!reactions.prepare("DELETE FROM reactions WHERE peer = ? AND target NOT IN ("
                           "SELECT e2eId FROM messages WHERE peer = ? AND e2eId != '')")) {
        throw std::runtime_error("the reactions of trimmed messages could not be removed");
    }
    reactions.addBindValue(peer);
    reactions.addBindValue(peer);
    if (!reactions.exec()) {
        throw std::runtime_error("the reactions of trimmed messages could not be removed");
    }
    return removed;
}

qint64 TranscriptStore::pruneToLatest(const QString& peer, const int keep)
{
    if (peer.isEmpty() || keep < 0) {
        return 0;
    }
    Transaction change(db_);
    if (!change.begun()) {
        throw std::runtime_error("the database is busy");
    }
    const qint64 removed = pruneRowsOf(peer, keep);
    if (!change.commit()) {
        throw std::runtime_error("the change could not be committed");
    }
    return removed;
}

qint64 TranscriptStore::pruneEveryChatToLatest(const int keep)
{
    if (keep < 0) {
        return 0;
    }
    const QStringList peers = conversationPeers();
    Transaction change(db_);
    if (!change.begun()) {
        throw std::runtime_error("the database is busy");
    }
    qint64 removed = 0;
    for (const QString& peer : peers) {
        removed += pruneRowsOf(peer, keep);
    }
    if (!change.commit()) {
        throw std::runtime_error("the change could not be committed");
    }
    return removed;
}

bool TranscriptStore::rebuild(QString& reason)
{
    const QFileInfo file(path_);
    if (QStorageInfo(file.absolutePath()).bytesAvailable() < file.size()) {
        reason = QStringLiteral("there is not enough free disk space to rewrite the database");
        return false;
    }
    Query vacuum(db_);
    if (!vacuum.exec("VACUUM")) {
        reason = QString::fromUtf8(sqlite3_errmsg(db_));
        return false;
    }
    Query checkpoint(db_);
    if (!checkpoint.exec("PRAGMA wal_checkpoint(TRUNCATE)")) {
        reason = QString::fromUtf8(sqlite3_errmsg(db_));
        return false;
    }
    if (checkpoint.value(0).toInt() != 0) {
        reason = QStringLiteral("the database is still being read");
        return false;
    }
    return true;
}

}  // namespace bazarish::app
