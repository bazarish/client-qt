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

// Column list shared by every full-row query, so the indices below stay aligned.
// orderKey is appended last so the existing 0..16 indices are unchanged.
const char* const kMessageColumns = "id, peer, outgoing, type, protocolId, text, attName,"
                                    " attMime, attSize, attRef, attKey, attSrcPath, keyboard,"
                                    " edited, ts, status, orderKey, savedPath, blobGone, replyTo,"
                                    " hasPicture, attDurationMs, attWave";

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
    m.hasPicture = query.value(20).toInt() != 0;
    m.attDurationMs = query.value(21).toLongLong();
    m.attWave = query.value(22).toString();
    m.replyTo = query.value(19).toString();
    return m;
}

}  // namespace

TranscriptStore::TranscriptStore() = default;

TranscriptStore::~TranscriptStore()
{
    sqlite3_close(db_);
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
    if (!readable(db_)) {
        // Wrong key: the pages do not decrypt. Say so by failing the open.
        sqlite3_close(db_);
        db_ = nullptr;
        return false;
    }

    Query query(db_);
    if (!query.exec(
            "CREATE TABLE IF NOT EXISTS messages ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "peer TEXT NOT NULL, outgoing INTEGER, type TEXT, protocolId TEXT,"
            "text TEXT, attName TEXT, attMime TEXT, attSize INTEGER,"
            "attRef TEXT, attKey TEXT, attSrcPath TEXT, keyboard TEXT, edited INTEGER,"
            " ts INTEGER, status INTEGER, orderKey INTEGER, savedPath TEXT,"
            " blobGone INTEGER, replyTo TEXT, hasPicture INTEGER,"
            " attDurationMs INTEGER, attWave TEXT)")) {
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
    return true;
}

qint64 TranscriptStore::append(const StoredMessage& message)
{
    Query query(db_);
    query.prepare(
        "INSERT INTO messages (peer, outgoing, type, protocolId, text, attName, attMime,"
        " attSize, attRef, attKey, attSrcPath, keyboard, edited, ts, status, orderKey, replyTo,"
        " attDurationMs, attWave)"
        " VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)");
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
    query.addBindValue(message.attDurationMs);
    query.addBindValue(message.attWave);
    if (!query.exec()) {
        return 0;
    }
    const qint64 id = query.lastInsertId();
    return id;
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

int TranscriptStore::failUnsentOnLoad(int sendingStatus, int failedStatus)
{
    Query query(db_);
    query.prepare("UPDATE messages SET status = ? WHERE outgoing = 1 AND status = ?");
    query.addBindValue(failedStatus);
    query.addBindValue(sendingStatus);
    if (!query.exec()) {
        return 0;
    }
    const int changed = query.numRowsAffected();
    if (changed > 0) {
    }
    return changed;
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

void TranscriptStore::setBlobGone(qint64 id, bool gone)
{
    Query query(db_);
    query.prepare("UPDATE messages SET blobGone = ? WHERE id = ?");
    query.addBindValue(gone ? 1 : 0);
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

qint64 TranscriptStore::idForProtocol(const QString& protocolId) const
{
    if (protocolId.isEmpty()) {
        return 0;
    }
    Query query(db_);
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
    Query query(db_);
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
    Query query(db_);
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
    Query query(db_);
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
    Query query(db_);
    query.prepare("UPDATE messages SET text = ?, keyboard = ?, edited = 1 WHERE id = ?");
    query.addBindValue(text);
    query.addBindValue(keyboard);
    query.addBindValue(id);
    if (query.exec()) {
    }
}

void TranscriptStore::removeById(qint64 id)
{
    Query query(db_);
    query.prepare("DELETE FROM messages WHERE id = ?");
    query.addBindValue(id);
    if (query.exec()) {
    }
}

void TranscriptStore::clearPeer(const QString& peer)
{
    Query query(db_);
    query.prepare("DELETE FROM messages WHERE peer = ?");
    query.addBindValue(peer);
    if (query.exec()) {
    }
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

}  // namespace bazarish::app
