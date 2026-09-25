// Bazarish project (c) 2026
#include "TranscriptStore.hpp"

#include "AccountKey.hpp"

#include <bazarish/Bytes.hpp>
// Qt makes `emit` a macro and the log header declares a function of that name,
// so the keyword is stood down for the length of this include.
#pragma push_macro("emit")
#undef emit
#include <bazarish/Log.hpp>
#pragma pop_macro("emit")

#include <QFileInfo>
#include <QStorageInfo>
#include <QStringList>

#include <sqlcipher/sqlite3.h>

#include <algorithm>
#include <stdexcept>
#include <string>

namespace bazarish::app {

namespace {

// One value read out of a result row, in the shape the readers below expect.
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

// A prepared statement in the shape the call sites are written against: prepare,
// bind in order, exec or next, read by column. Thin on purpose - it exists so the
// queries below read as queries and every sqlite3 handle has an owner.
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

    // Runs a prepared statement that returns nothing (or whose rows are ignored).
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

    // Prepares and runs a statement in one call (the DDL and the parameterless
    // selects below).
    bool exec(const QString& sql) { return prepare(sql) && exec(); }

    // Advances to the next row; true while there is one.
    bool next()
    {
        if (stepped_) {
            stepped_ = false;  // exec() already stepped onto the first row
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

// Column list shared by every full-row query, so the indices in readMessageRow
// stay aligned with it. Change one and change the other.
const char* const kMessageColumns = "id, peer, outgoing, type, e2eId, text, attName,"
                                    " attMime, attSize, attRef, attSrcPath, keyboard,"
                                    " edited, ts, status, orderKey, savedPath, replyTo,"
                                    " hasPicture, attDurationMs, attWave, forwarded";

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
    return m;
}

}  // namespace

TranscriptStore::TranscriptStore() = default;

TranscriptStore::~TranscriptStore()
{
    close();
}

void TranscriptStore::close()
{
    sqlite3_close(db_);  // a no-op on a connection already closed
    db_ = nullptr;
    path_.clear();
}

namespace {

// Opens a connection and unlocks it with `key`. The pragma has to be the first
// statement on the connection.
sqlite3* openKeyed(const QString& path, const Bytes& key)
{
    sqlite3* db = nullptr;
    if (sqlite3_open(path.toUtf8().constData(), &db) != SQLITE_OK) {
        sqlite3_close(db);
        return nullptr;
    }
    // SQLCipher reports a failed decryption on stderr; the caller reports it
    // through the return value instead, so the library's own chatter is off.
    sqlite3_exec(db, "PRAGMA cipher_log_level = NONE", nullptr, nullptr, nullptr);
    // The account's other connection - the one the worker thread holds - waits
    // this long for a lock. This one never did, and it is this one that takes the
    // database exclusively when history is trimmed.
    constexpr int kBusyTimeoutMs = 5000;
    sqlite3_busy_timeout(db, kBusyTimeoutMs);
    // The raw key goes in as a blob literal, so SQLCipher derives nothing: the
    // passphrase guards the key file beside the database (see AccountKey).
    const std::string pragma = "PRAGMA key = \"x'" + toHex(key) + "'\"";
    if (sqlite3_exec(db, pragma.c_str(), nullptr, nullptr, nullptr) != SQLITE_OK) {
        sqlite3_close(db);
        return nullptr;
    }
    return db;
}

// Whether the connection can actually read the database: with the wrong key the
// pages do not decrypt and the first read fails.
bool readable(sqlite3* const db)
{
    return sqlite3_exec(db, "SELECT count(*) FROM sqlite_master", nullptr, nullptr, nullptr)
        == SQLITE_OK;
}

}  // namespace

bool TranscriptStore::open(const QString& accountId, const QString& dbPath, const QString& passphrase)
{
    (void)accountId;  // one connection per store now; the id no longer names it
    // The same key the account store holds: unwrapped once per process. A wrong
    // passphrase cannot unwrap it, and this store answers that with false rather
    // than an exception - a failed open, never an empty transcript.
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
    path_ = dbPath;
    if (!readable(db_)) {
        // Wrong key: the pages do not decrypt. Say so by failing the open.
        sqlite3_close(db_);
        db_ = nullptr;
        return false;
    }

    Query query(db_);
    // What schema this database was written against. SQLite carries the number
    // itself (user_version), so it costs no table and cannot get out of step with
    // the tables it describes. A database from before the number existed reads as
    // 0 and is told apart from a fresh one by whether it holds anything.
    Query version(db_);
    std::int64_t schema = 0;
    if (version.exec("PRAGMA user_version") && version.next()) {
        schema = version.value(0).toLongLong();
    }
    // Asked of the catalogue rather than of the table itself: a fresh database has
    // no messages table, and probing for one would report a failure that is the
    // normal case for every account ever created.
    Query written(db_);
    const bool hasTables
        = written.exec("SELECT 1 FROM sqlite_master WHERE type = 'table' AND name = 'messages'")
        && written.next();
    if (hasTables) {
        // The number alone cannot catch a column added before the first release,
        // when the number is frozen: ask the table itself whether it holds what
        // this build writes.
        Query columns(db_);
        if (!columns.exec(QStringLiteral("SELECT %1 FROM messages LIMIT 1").arg(kMessageColumns))) {
            throw std::runtime_error(
                "this account was written against a different set of columns and cannot be read;"
                " create it again");
        }
    }
    if (hasTables && schema != kAccountSchemaVersion) {
        // Either older than the numbering or newer than this build understands.
        // Both are refused with the number, because a migration that does not
        // exist yet must not be improvised at runtime.
        throw std::runtime_error("this account is schema version " + std::to_string(schema)
            + ", and this build reads version " + std::to_string(kAccountSchemaVersion));
    }
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
    // Stamp the number on a database that has just been laid out.
    if (!hasTables
        && !query.exec(
            "PRAGMA user_version = " + QString::number(kAccountSchemaVersion))) {
        return false;
    }
    // Chats the user pinned to the top of the list (one row per pinned peer).
    if (!query.exec("CREATE TABLE IF NOT EXISTS pinned_chats (peer TEXT PRIMARY KEY)")) {
        return false;
    }
    // The three below tidy what is already there; none of them is what makes the
    // account readable, so one that fails is said out loud and the account still
    // opens. Refusing to open it over a housekeeping statement would cost the
    // user their messages to save them from a duplicate.
    //
    // Calls used to leave a line in the conversation and a preview in the chat
    // list. They no longer do, and the lines already written go with them: these
    // are the only system notes this client ever wrote with those openings.
    if (!query.exec("DELETE FROM messages WHERE type = 'system' AND ("
                    "text LIKE 'Incoming call%' OR text LIKE 'Outgoing call%'"
                    " OR text = 'Missed call')")) {
        bazarish::log::warn("transcript: the call lines could not be cleared");
    }
    // One message, one row. The rule is the index below; this clears what was
    // written before there was one, keeping the copy that arrived first.
    if (!query.exec("DELETE FROM messages WHERE e2eId != '' AND id NOT IN ("
                    "SELECT MIN(id) FROM messages WHERE e2eId != ''"
                    " GROUP BY peer, e2eId, outgoing)")) {
        bazarish::log::warn("transcript: the duplicate rows could not be cleared");
    }
    // A message is named by its protocol id, and the same id in the same
    // conversation on the same side is the same message however many times it is
    // offered. System notes carry no id and are not constrained. This is the
    // backstop; what every row goes through first is append().
    if (!query.exec("CREATE UNIQUE INDEX IF NOT EXISTS messages_by_e2e"
                    " ON messages (peer, e2eId, outgoing) WHERE e2eId != ''")) {
        bazarish::log::warn("transcript: one message per row is not enforced here");
    }
    // Every per-conversation statement stands on this: the index above is partial
    // and a plain "where peer = ?" cannot use it, so a windowed read, a preview
    // and a trim each scanned the whole table. One conversation is one scan is
    // survivable; trimming every conversation would be one scan per conversation.
    if (!query.exec("CREATE INDEX IF NOT EXISTS messages_by_peer_id ON messages (peer, id)")) {
        bazarish::log::warn("transcript: conversations are read without an index");
    }
    return true;
}

qint64 TranscriptStore::append(const StoredMessage& message)
{
    // At-least-once delivery means the same message legitimately arrives more
    // than once; it is one message either way, and this is the one place every
    // row goes through.
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
        // The index above is the backstop for a path that did not ask first. Say
        // which row it is rather than answering with an id nobody has.
        const qint64 existing = message.e2eId.isEmpty()
            ? 0
            : idForKey(message.peer, message.e2eId, message.outgoing);
        if (existing != 0) {
            bazarish::log::warn("transcript: {} was stored without asking first",
                message.e2eId.toStdString());
        }
        return existing;
    }
    const qint64 id = query.lastInsertId();
    return id;
}

qint64 TranscriptStore::idForKey(
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

void TranscriptStore::updateStatus(qint64 id, int status)
{
    Query query(db_);
    query.prepare("UPDATE messages SET status = ? WHERE id = ?");
    query.addBindValue(status);
    query.addBindValue(id);
    if (query.exec()) {
    }
}

QVector<StoredMessage> TranscriptStore::messagesFor(const QString& peer) const
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

QVector<StoredMessage> TranscriptStore::latestMessages(const QString& peer, int limit) const
{
    // Newest `limit` rows, returned oldest-first (the display order). DESC+LIMIT
    // reads only the tail of a huge conversation; the result is then reversed.
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

QVector<StoredMessage> TranscriptStore::olderMessages(
    const QString& peer, qint64 beforeId, int limit) const
{
    // The `limit` rows immediately older than beforeId, oldest-first.
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

StoredMessage TranscriptStore::nextVoiceAfter(const QString& peer, const qint64 afterId) const
{
    StoredMessage found;
    Query query(db_);
    query.prepare(QStringLiteral("SELECT %1 FROM messages WHERE peer = ? AND id > ?"
                                 " AND type = 'voice' ORDER BY id ASC LIMIT 1")
                      .arg(kMessageColumns));
    query.addBindValue(peer);
    query.addBindValue(afterId);
    if (!query.exec() || !query.next()) {
        return found;
    }
    return readMessageRow(query);
}

QVector<StoredMessage> TranscriptStore::newerMessages(
    const QString& peer, qint64 afterId, int limit) const
{
    // The `limit` rows immediately newer than afterId, already oldest-first.
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

bool TranscriptStore::hasMessagesBefore(const QString& peer, qint64 id) const
{
    Query query(db_);
    query.prepare("SELECT 1 FROM messages WHERE peer = ? AND id < ? LIMIT 1");
    query.addBindValue(peer);
    query.addBindValue(id);
    return query.exec() && query.next();
}

bool TranscriptStore::hasMessagesAfter(const QString& peer, qint64 id) const
{
    Query query(db_);
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
    Query sql(db_);
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

int TranscriptStore::failUnsentOnLoad(
    const int preparingStatus, const int deliveringStatus, const int failedStatus)
{
    Query query(db_);
    query.prepare(
        "UPDATE messages SET status = ? WHERE outgoing = 1 AND status IN (?, ?)");
    query.addBindValue(failedStatus);
    query.addBindValue(preparingStatus);
    query.addBindValue(deliveringStatus);
    if (!query.exec()) {
        return 0;
    }
    return query.numRowsAffected();
}

int TranscriptStore::settleUnfinishedNotes(const QString& type, const int preparingStatus,
    const int settledStatus, const QString& text)
{
    Query query(db_);
    query.prepare("UPDATE messages SET status = ?, text = ? WHERE type = ? AND status = ?");
    query.addBindValue(settledStatus);
    query.addBindValue(text);
    query.addBindValue(type);
    query.addBindValue(preparingStatus);
    if (!query.exec()) {
        return 0;
    }
    return query.numRowsAffected();
}

void TranscriptStore::markOutgoingReadUpTo(
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
    if (query.exec() && query.numRowsAffected() > 0) {
    }
}

QString TranscriptStore::sourcePathFor(qint64 id) const
{
    Query query(db_);
    query.prepare("SELECT attSrcPath FROM messages WHERE id = ? LIMIT 1");
    query.addBindValue(id);
    if (query.exec() && query.next()) {
        return query.value(0).toString();
    }
    return {};
}

void TranscriptStore::setSavedPath(qint64 id, const QString& path)
{
    Query query(db_);
    query.prepare("UPDATE messages SET savedPath = ? WHERE id = ?");
    query.addBindValue(path);
    query.addBindValue(id);
    if (query.exec()) {
    }
}

QByteArray TranscriptStore::media(const QString& key) const
{
    Query query(db_);
    // The same table the core keeps its state in: one account, one database.
    if (!query.prepare("SELECT value FROM state WHERE name = ?")) {
        return {};
    }
    query.addBindValue(key);
    if (!query.exec() || !query.next()) {
        return {};
    }
    return query.value(0).toByteArray();
}

void TranscriptStore::setHasPicture(const qint64 id, const bool has)
{
    Query query(db_);
    query.prepare("UPDATE messages SET hasPicture = ? WHERE id = ?");
    query.addBindValue(has ? 1 : 0);
    query.addBindValue(id);
    if (!query.exec()) {
        bazarish::log::warn("could not record that a message holds a picture");
    }
}

qint64 TranscriptStore::idForE2e(const QString& e2eId) const
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

qint64 TranscriptStore::idForIncomingE2e(const QString& e2eId, const QString& peer) const
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

qint64 TranscriptStore::oldestOfType(
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

qint64 TranscriptStore::idForAnyProtocol(const QString& e2eId, const QString& peer) const
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

StoredMessage TranscriptStore::messageByE2e(
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

void TranscriptStore::editContent(qint64 id, const QString& text, const QString& keyboard)
{
    Query query(db_);
    query.prepare("UPDATE messages SET text = ?, keyboard = ?, edited = 1 WHERE id = ?");
    query.addBindValue(text);
    query.addBindValue(keyboard);
    query.addBindValue(id);
    if (query.exec()) {
    }
}

void TranscriptStore::setType(qint64 id, const QString& type)
{
    Query query(db_);
    query.prepare("UPDATE messages SET type = ? WHERE id = ?");
    query.addBindValue(type);
    query.addBindValue(id);
    if (query.exec()) {
    }
}

void TranscriptStore::removeById(qint64 id)
{
    // Read before the row goes: the message is what names its picture or voice
    // note, and once it is gone nothing does.
    QString named;
    Query naming(db_);
    if (naming.prepare("SELECT e2eId FROM messages WHERE id = ?")) {
        naming.addBindValue(id);
        if (naming.exec() && naming.next()) {
            named = naming.value(0).toString();
        }
    }
    if (!removeMediaOfMessage(named)) {
        bazarish::log::warn("transcript: the picture or voice note of a deleted message stayed");
    }
    // A reaction names its target by the message's own id, so one left behind is
    // a record that this account was reacted to, kept after the thing it points
    // at is gone.
    if (!named.isEmpty()) {
        Query emoji(db_);
        emoji.prepare("DELETE FROM reactions WHERE target = ?");
        emoji.addBindValue(named);
        if (!emoji.exec()) {
            bazarish::log::warn("transcript: the reactions of a deleted message stayed");
        }
    }
    Query query(db_);
    query.prepare("DELETE FROM messages WHERE id = ?");
    query.addBindValue(id);
    if (!query.exec()) {
        bazarish::log::warn("transcript: a message could not be deleted");
    }
}

void TranscriptStore::clearPeer(const QString& peer)
{
    // The media first, while the rows that name it are still there: a cleared
    // conversation used to leave every picture and voice note it held in the
    // database for good.
    if (!removeMediaOfPeer(peer)) {
        bazarish::log::warn("transcript: the media of a cleared conversation stayed behind");
    }
    Query query(db_);
    query.prepare("DELETE FROM messages WHERE peer = ?");
    query.addBindValue(peer);
    if (!query.exec()) {
        bazarish::log::warn("transcript: a conversation could not be cleared");
    }
    // What was about those messages goes with them: every reaction, and the read
    // mark, which now points at a row that is not there. The pin is not among
    // them - it is about the chat, which an emptied one still is.
    for (const char* statement :
        {"DELETE FROM reactions WHERE peer = ?", "DELETE FROM read_state WHERE peer = ?"}) {
        Query side(db_);
        side.prepare(QString::fromUtf8(statement));
        side.addBindValue(peer);
        if (!side.exec()) {
            bazarish::log::warn("transcript: something of a cleared conversation stayed behind");
        }
    }
}

void TranscriptStore::forgetPeer(const QString& peer)
{
    // A contact that is gone, as against a conversation that is merely empty:
    // nothing of theirs is kept, the pin included.
    clearPeer(peer);
    setPinned(peer, false);
}

QStringList TranscriptStore::conversationPeers() const
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

QString TranscriptStore::lastText(const QString& peer) const
{
    Query query(db_);
    // The newest row that has something to show, not simply the newest row. A row
    // with no words and nothing carried - a control message stored for its own
    // reasons - would otherwise leave the chat list saying nothing at all about a
    // conversation that is full of messages.
    query.prepare("SELECT text, type, attName FROM messages WHERE peer = ?"
                  " AND (text != '' OR type IN ('file','image','voice','audio','photo'))"
                  " ORDER BY orderKey DESC, id DESC LIMIT 1");
    query.addBindValue(peer);
    if (query.exec() && query.next()) {
        const QString text = query.value(0).toString();
        const QString type = query.value(1).toString();
        const QString attachment = query.value(2).toString();
        if (!text.isEmpty()) {
            return text;
        }
        // A message with no words of its own is named by what it carries, so the
        // chat list says something rather than nothing. The names are the ones
        // the transcript stores, which is what a picture is written as.
        if (type == "file" || type == "image" || type == "voice" || type == "audio"
            || type == "photo") {
            return attachment.isEmpty() ? "[" + type + "]" : "[" + type + "] " + attachment;
        }
    }
    return {};
}

qint64 TranscriptStore::lastTime(const QString& peer) const
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

void TranscriptStore::setLastReadId(const QString& peer, qint64 id)
{
    if (peer.isEmpty() || id <= 0) {
        return;
    }
    Query query(db_);
    // Never lower the high-water (a re-read of older history must not resurrect
    // newer messages as unread). Written without UPSERT, which arrived in SQLite
    // 3.24 and is not in the SQLCipher some distributions ship: the row is
    // replaced with whichever mark is further along.
    query.prepare("INSERT OR REPLACE INTO read_state (peer, last_read_id) VALUES (?,"
                  " max(?, coalesce((SELECT last_read_id FROM read_state WHERE peer = ?), 0)))");
    query.addBindValue(peer);
    query.addBindValue(id);
    query.addBindValue(peer);
    if (query.exec() && query.numRowsAffected() > 0) {
    }
}

void TranscriptStore::applyReadThrough(const QString& peer, const qint64 sentAtMs)
{
    if (peer.isEmpty() || sentAtMs <= 0) {
        return;
    }
    Query query(db_);
    // The newest incoming row at or before that moment, and never below the mark
    // already held: like every other write of this high-water, it only advances.
    if (!query.prepare("INSERT OR REPLACE INTO read_state (peer, last_read_id) VALUES (?,"
                       " max(coalesce((SELECT last_read_id FROM read_state WHERE peer = ?), 0),"
                       " coalesce((SELECT MAX(id) FROM messages WHERE peer = ? AND outgoing = 0"
                       " AND type != 'system' AND ts <= ?), 0)))")) {
        return;
    }
    query.addBindValue(peer);
    query.addBindValue(peer);
    query.addBindValue(peer);
    query.addBindValue(sentAtMs);
    if (!query.exec()) {
        bazarish::log::warn("transcript: a read mark from another device was not applied");
    }
}

qint64 TranscriptStore::lastReadId(const QString& peer) const
{
    Query query(db_);
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
    Query query(db_);
    if (pinned) {
        query.prepare("INSERT OR IGNORE INTO pinned_chats (peer) VALUES (?)");
    } else {
        query.prepare("DELETE FROM pinned_chats WHERE peer = ?");
    }
    query.addBindValue(peer);
    if (query.exec() && query.numRowsAffected() > 0) {
    }
}

bool TranscriptStore::isPinned(const QString& peer) const
{
    Query query(db_);
    query.prepare("SELECT 1 FROM pinned_chats WHERE peer = ? LIMIT 1");
    query.addBindValue(peer);
    return query.exec() && query.next();
}

QStringList TranscriptStore::pinnedPeers() const
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

int TranscriptStore::unreadCount(const QString& peer) const
{
    Query query(db_);
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

void TranscriptStore::setReaction(
    const QString& peer, const QString& target, const QString& reactor, const QString& emoji)
{
    if (peer.isEmpty() || target.isEmpty() || reactor.isEmpty()) {
        return;
    }
    Query query(db_);
    if (emoji.isEmpty()) {
        // An empty emoji clears the reactor's reaction on this message.
        query.prepare("DELETE FROM reactions WHERE peer = ? AND target = ? AND reactor = ?");
        query.addBindValue(peer);
        query.addBindValue(target);
        query.addBindValue(reactor);
    } else {
        // The key is (peer, target, reactor) and emoji is the whole value, so
        // replacing the row is the same as updating it - and works on the older
        // SQLite that has no UPSERT.
        query.prepare("INSERT OR REPLACE INTO reactions (peer, target, reactor, emoji)"
                      " VALUES (?, ?, ?, ?)");
        query.addBindValue(peer);
        query.addBindValue(target);
        query.addBindValue(reactor);
        query.addBindValue(emoji);
    }
    if (query.exec()) {
    }
}

QVector<Reaction> TranscriptStore::reactionsFor(const QString& peer, const QString& target) const
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

// Where the core keeps a message's media, under the same names it writes them.
const char* const kPictureKeyPrefix = "picture:";
const char* const kVoiceKeyPrefix = "voice:";

// The bytes one message row's own values hold. LENGTH counts characters over
// TEXT, so every column is measured as a blob instead. The fixed-width columns
// share one flat allowance: their width is per row, and this whole figure is an
// account of content rather than a measurement of pages.
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

// The media of the messages a SELECT picks, by the reference each row carries.
// Rows with no protocol id name nothing, and are left out here rather than
// producing a key of their own.
QString mediaOfRows(const QString& rowCondition)
{
    return QStringLiteral("DELETE FROM state WHERE name IN ("
                          "SELECT '%1' || e2eId FROM messages WHERE e2eId != '' AND (%3)"
                          " UNION ALL"
                          " SELECT '%2' || e2eId FROM messages WHERE e2eId != '' AND (%3))")
        .arg(QString::fromUtf8(kPictureKeyPrefix), QString::fromUtf8(kVoiceKeyPrefix),
            rowCondition);
}

// One change, taken as one. IMMEDIATE asks for the write lock up front instead
// of discovering halfway through that the other connection holds it.
class Transaction {
public:
    explicit Transaction(sqlite3* const db)
        : db_(db)
    {
        Query begin(db_);
        if (!begin.exec("BEGIN IMMEDIATE")) {
            throw std::runtime_error("the database is busy");
        }
    }
    ~Transaction()
    {
        if (committed_) {
            return;
        }
        Query rollback(db_);
        if (!rollback.exec("ROLLBACK")) {
            bazarish::log::warn("transcript: a change could not be rolled back");
        }
    }

    Transaction(const Transaction&) = delete;
    Transaction& operator=(const Transaction&) = delete;

    void commit()
    {
        Query end(db_);
        if (!end.exec("COMMIT")) {
            throw std::runtime_error("the change could not be committed");
        }
        committed_ = true;
    }

private:
    sqlite3* db_ = nullptr;
    bool committed_ = false;
};

}  // namespace

bool TranscriptStore::hasMediaTable() const
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
    if (!query.prepare("DELETE FROM state WHERE name = ? OR name = ?")) {
        return false;
    }
    query.addBindValue(QString::fromUtf8(kPictureKeyPrefix) + e2eId);
    query.addBindValue(QString::fromUtf8(kVoiceKeyPrefix) + e2eId);
    return query.exec();
}

bool TranscriptStore::removeMediaOfPeer(const QString& peer)
{
    if (!hasMediaTable()) {
        return true;
    }
    Query query(db_);
    if (!query.prepare(mediaOfRows("peer = ?"))) {
        return false;
    }
    query.addBindValue(peer);
    query.addBindValue(peer);
    return query.exec();
}

bool TranscriptStore::removeMediaOfTrimmed(const QString& peer, const int keep)
{
    if (!hasMediaTable()) {
        return true;
    }
    // The same rows the trim is about to remove, named the same way.
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

QVector<ChatWeight> TranscriptStore::chatWeights() const
{
    QVector<ChatWeight> weights;
    // messages is the outer loop and the media table is probed by its primary
    // key, which is what the outer joins pin down: the other order has no index
    // to stand on and would read every blob for every message.
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

DatabaseFootprint TranscriptStore::footprint() const
{
    DatabaseFootprint out;
    out.fileBytes = QFileInfo(path_).size();
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
    // The media of the rows about to go, while they are still there to name it.
    if (!removeMediaOfTrimmed(peer, keep)) {
        throw std::runtime_error("the media of trimmed messages could not be removed");
    }
    // Newest by id, which is the key the windowed reads page by: trimming on any
    // other order would keep a different set than the conversation shows.
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
    const qint64 removed = pruneRowsOf(peer, keep);
    change.commit();
    return removed;
}

qint64 TranscriptStore::pruneEveryChatToLatest(const int keep)
{
    if (keep < 0) {
        return 0;
    }
    const QStringList peers = conversationPeers();
    Transaction change(db_);
    qint64 removed = 0;
    for (const QString& peer : peers) {
        removed += pruneRowsOf(peer, keep);
    }
    change.commit();
    return removed;
}

bool TranscriptStore::rebuild(QString& reason)
{
    const QFileInfo file(path_);
    // A rewrite is a whole second copy of the database before it replaces the
    // first, so the room for one is checked rather than discovered.
    if (QStorageInfo(file.absolutePath()).bytesAvailable() < file.size()) {
        reason = QStringLiteral("there is not enough free disk space to rewrite the database");
        return false;
    }
    Query vacuum(db_);
    if (!vacuum.exec("VACUUM")) {
        reason = QString::fromUtf8(sqlite3_errmsg(db_));
        return false;
    }
    // An account whose schema mark is not the one it was written against is
    // refused at open, so the mark is put back rather than trusted to a rewrite.
    qint64 stamped = 0;
    Query version(db_);
    if (version.exec("PRAGMA user_version") && version.next()) {
        stamped = version.value(0).toLongLong();
    }
    if (stamped == kAccountSchemaVersion) {
        return true;
    }
    Query restore(db_);
    if (!restore.exec("PRAGMA user_version = " + QString::number(kAccountSchemaVersion))) {
        reason = QStringLiteral("the database was rewritten but its schema mark was lost");
        return false;
    }
    bazarish::log::warn("transcript: the schema mark was written again after a rebuild");
    return true;
}

}  // namespace bazarish::app
