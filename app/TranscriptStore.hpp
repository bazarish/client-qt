// Bazarish project (c) 2026
#pragma once

#include <QByteArray>
#include <QString>
#include <QVector>

#include <cstdint>
#include <string>

// The SQLCipher connection handle, opaque here (sqlite3.h stays out of this
// header, which the QML layer includes).
struct sqlite3;

namespace bazarish::app {

// One stored conversation entry. Mirrors the rendered message; bodies live in
// a per-account SQLite database under the account directory.
struct StoredMessage {
    qint64 id = 0;
    QString peer;          // contact fingerprint
    bool outgoing = false;
    QString type;          // content type: text/file/photo/... or "system"
    QString e2eId;    // envelope message id (to match delivery receipts)
    QString text;
    QString attName;
    QString attMime;
    qint64 attSize = 0;
    qint64 attDurationMs = 0;  // a voice message's length
    QString attWave;       // a voice message's loudness account, one hex digit a bar
    QString attRef;        // content-store id
    QString attKey;        // base64 content key
    QString attSrcPath;    // local source path of an outgoing attachment (for resend)
    QString savedPath;     // where an incoming attachment was last saved (local path)
    bool blobGone = false; // incoming attachment whose blob is gone from the store
    // A picture this account holds in its database (drawn in the bubble).
    bool hasPicture = false;
                           // (download returned 404/410); shows "Not found", no Save
    QString keyboard;      // inline-keyboard JSON (empty when none)
    QString replyTo;       // protocol id of the message this one replies to (empty
                           // when not a reply); the UI resolves it to a local row
    bool edited = false;   // true once the message was edited in place
    // The sender passed this on rather than writing it. A bare mark: it names
    // nobody, and says nothing about who wrote what it carries.
    bool forwarded = false;
    qint64 ts = 0;         // unix milliseconds - the message's sentAt (display time)
    qint64 orderKey = 0;   // unix ms sort position: sentAt for a recent arrival,
                           // the local arrival time for a late one (see append path)
    int status = 0;        // 0 sending, 1 sent, 2 failed, 3 received
};

// One full-text search match within a conversation (lightweight: enough to list
// the hit and jump to it, without the attachment fields).
struct SearchHit {
    qint64 id = 0;
    qint64 ts = 0;
    QString text;
    bool outgoing = false;
};

// One reaction on a message: who reacted (fingerprint) and the emoji. One per
// reactor per message - a new emoji from the same reactor overwrites the old.
struct Reaction {
    QString reactor;
    QString emoji;
};

// Persistent local message log for one account, in the account's SQLCipher
// database. The file is encrypted page by page, so a message is written where it
// belongs instead of re-sealing the whole history on every change. The database
// key is the random one kept beside the file (see AccountKey), so opening costs
// nothing once the account is unlocked.
// The schema every account database is written against. It is stamped into the
// file (SQLite's user_version) when the tables are laid out and checked on every
// open: a database written against another number is refused rather than read
// through guesswork. Frozen at 1 until the first release - before then a schema
// change means a new account, not a migration; after it, this is the number a
// migration steps from.
inline constexpr int kAccountSchemaVersion = 1;

// The two depths a conversation can be trimmed to. Named here because they are
// the offer the interface makes, and it makes it with these numbers.
inline constexpr int kKeepRecentMessages = 100;
inline constexpr int kKeepManyMessages = 1000;

// What one conversation weighs in the account database: the values its rows hold
// plus the media blobs those rows name. Content, not pages on disk - the file is
// always larger, and no query can attribute a page back to a conversation.
struct ChatWeight {
    QString peer;
    qint64 messages = 0;
    qint64 rowBytes = 0;
    qint64 mediaBytes = 0;
    qint64 mediaCount = 0;
};

// The database file: what it takes on disk, and the part of that a rebuild would
// return. The free figure counts pages at the plaintext page size, so it is a
// little under what a rebuild actually recovers.
struct DatabaseFootprint {
    qint64 fileBytes = 0;
    qint64 freeBytes = 0;
};

class TranscriptStore {
public:
    TranscriptStore();
    ~TranscriptStore();

    // Lets go of the database file. Called by the destructor, and directly when
    // the account is being removed: the file has to be closed before it is
    // deleted, and the object itself outlives that moment.
    void close();

    // Opens (and creates) the database for this account id. Returns false when it
    // cannot be opened - a wrong passphrase is a failed open, never an empty
    // transcript.
    bool open(const QString& accountId, const QString& dbPath, const QString& passphrase = {});

    qint64 append(const StoredMessage& message);
    void updateStatus(qint64 id, int status);
    // Marks every outgoing message left in a status only a running delivery can
    // hold (there is no outbound queue on disk, so on load these are sends this
    // client was carrying when it closed, not ones in flight) as failedStatus, so
    // the UI shows "not sent" with a resend option instead of a perpetual upload
    // animation. Returns the number changed.
    int failUnsentOnLoad(int preparingStatus, int deliveringStatus, int failedStatus);
    // A contact add writes its progress into the conversation and leaves the row
    // in preparingStatus while it runs. A row still in that state at open belongs
    // to a run that ended before the add did: nothing is working on it, and it
    // must not go on saying that something is. Returns how many were settled.
    int settleUnfinishedNotes(const QString& type, int preparingStatus, int settledStatus,
        const QString& text);
    // Marks outgoing messages to peer with id <= uptoId whose status is within
    // [minStatus, maxStatus] as readStatus (the recipient read up to uptoId). Used
    // to persist the green "read" state high-water, including paged-out rows.
    void markOutgoingReadUpTo(
        const QString& peer, qint64 uptoId, int readStatus, int minStatus, int maxStatus);
    // The local source path recorded for an outgoing attachment (empty if none).
    QString sourcePathFor(qint64 id) const;
    // Records where an incoming attachment was saved, so the UI can later offer to
    // open it (and fall back to re-saving if the file is gone).
    void setSavedPath(qint64 id, const QString& path);
    // Marks an incoming attachment whose blob is no longer on the store (the
    // download returned 404/410): the bubble then shows "Not found" with no Save,
    // a state that survives a restart.
    // A media blob kept in the account (a picture, a voice message), by the same
    // key the core stores it under. Read here rather than through the session
    // worker: this side already holds the account open, and a picture must not
    // wait behind a sync for its turn to be drawn.
    QByteArray media(const QString& key) const;

    // Marks a message whose picture this account holds.
    void setHasPicture(qint64 id, bool has);
    void setBlobGone(qint64 id, bool gone);
    qint64 idForE2e(const QString& e2eId) const;
    // The row a message occupies, if it has one: the conversation, the protocol
    // id and the side it is on name exactly one.
    qint64 idForKey(const QString& peer, const QString& e2eId, bool outgoing) const;
    // The row id of an incoming message from peer with this protocol id, the
    // target of an edit (0 if none). Scoping to incoming-from-peer is the
    // security check: a peer can only edit a message it actually sent.
    qint64 idForIncomingE2e(const QString& e2eId, const QString& peer) const;
    // The oldest row of one type on one side of a conversation (0 if none). A
    // conversation holds one contact-request plate per direction however many
    // requests were actually sent, so this is what a second one is checked
    // against.
    qint64 oldestOfType(const QString& peer, const QString& type, bool outgoing) const;
    // The row id of a message under `peer` with this protocol id, either direction
    // (0 if none). Used to resolve a reply reference to a local message to jump to.
    qint64 idForAnyProtocol(const QString& e2eId, const QString& peer) const;
    // The full row for a protocol id under `peer`, either direction (id 0 when not
    // found). Used to render a reply quote (author + text/file name of the original).
    StoredMessage messageByE2e(const QString& e2eId, const QString& peer) const;
    // Replaces a message's text and keyboard and marks it edited.
    void editContent(qint64 id, const QString& text, const QString& keyboard);
    // Permanently removes a message (delete with no trace).
    void removeById(qint64 id);
    // Permanently removes every message of a conversation (clear chat / a deleted
    // contact). The peer key is a contact fingerprint.
    void clearPeer(const QString& peer);
    // The same, for a contact that is being removed rather than a conversation
    // being emptied: the pin goes too, there being no chat left to pin.
    void forgetPeer(const QString& peer);
    QVector<StoredMessage> messagesFor(const QString& peer) const;
    // Windowed reads for paging a large conversation: the newest `limit` rows,
    // the `limit` rows just older than beforeId, and the `limit` rows just newer
    // than afterId - all returned oldest-first (the display order).
    QVector<StoredMessage> latestMessages(const QString& peer, int limit) const;
    QVector<StoredMessage> olderMessages(const QString& peer, qint64 beforeId, int limit) const;
    QVector<StoredMessage> newerMessages(const QString& peer, qint64 afterId, int limit) const;
    // The next voice message after afterId in this conversation, from either
    // side; empty when that was the last one. Playback runs on to it.
    StoredMessage nextVoiceAfter(const QString& peer, qint64 afterId) const;
    // Whether any row exists strictly older / newer than id (drives "load more").
    bool hasMessagesBefore(const QString& peer, qint64 id) const;
    bool hasMessagesAfter(const QString& peer, qint64 id) const;
    // Case-insensitive full-text matches within a conversation, newest first.
    QVector<SearchHit> searchInPeer(const QString& peer, const QString& query) const;
    QString lastText(const QString& peer) const;
    qint64 lastTime(const QString& peer) const;
    // Every distinct conversation key (a contact fingerprint) that has at least one
    // stored message. Lets the chat list surface a conversation whose contact record
    // was lost, so its transcript is never silently hidden.
    QStringList conversationPeers() const;

    // --- Read state (persistent unread tracking) ---
    // A per-peer high-water of the last locally-read incoming message id. It only
    // advances (a lower id is ignored), so re-reading older history never lowers it.
    // Survives restart, so the unread badge and the open-at-first-unread position
    // are accurate across sessions.
    void setLastReadId(const QString& peer, qint64 id);
    // Another device of this account read the conversation through a message sent
    // at this moment. Resolved against what is stored HERE and folded into the
    // high-water at once, rather than kept as a standing rule: a message that
    // turns up later carrying an older stamp has not been seen by anybody, and
    // the stamp is the sender's to write.
    void applyReadThrough(const QString& peer, qint64 sentAtMs);
    qint64 lastReadId(const QString& peer) const;

    // --- Pinned chats (kept at the top of the chat list; synced across devices) ---
    void setPinned(const QString& peer, bool pinned);
    bool isPinned(const QString& peer) const;
    QStringList pinnedPeers() const;
    // The number of incoming messages newer than the read high-water (the unread
    // badge count for this conversation).
    int unreadCount(const QString& peer) const;
    // The id of the oldest unread incoming message (0 when nothing is unread): the
    // row a conversation opens at, so the user lands on the first thing they missed.
    qint64 firstUnreadId(const QString& peer) const;

    // --- Reactions (one emoji per user per message; new overwrites old) ---
    // Sets `reactor`'s reaction to `target` (a message protocol id under `peer`) to
    // `emoji`; an empty emoji removes their reaction. Idempotent upsert.
    void setReaction(
        const QString& peer, const QString& target, const QString& reactor, const QString& emoji);
    // Every reaction on a message, for the bubble summary and the who-reacted list.
    QVector<Reaction> reactionsFor(const QString& peer, const QString& target) const;

    // --- What the account holds, and dropping old history ---

    // Every conversation with what it weighs, unordered.
    QVector<ChatWeight> chatWeights() const;
    // The file's size and the free space inside it.
    DatabaseFootprint footprint() const;
    // Keeps the newest `keep` messages of `peer` and removes the rest, with their
    // reactions and the media nothing names afterwards. Newest by id, the key the
    // windowed reads page by. One transaction: the conversation is trimmed or it
    // is untouched. Returns the messages removed, and throws when a statement
    // fails - a half-trimmed transcript is not a result worth returning.
    qint64 pruneToLatest(const QString& peer, int keep);
    qint64 pruneEveryChatToLatest(int keep);
    // Rewrites the file so the pages a trim freed leave it. Deliberately not part
    // of an ordinary deletion: its cost is the size of what is KEPT, so removing
    // one message from a large account would rewrite the whole of it - measured at
    // 4.7 s on 148 MB - and the same path carries a correspondent's delete. Only
    // the storage window asks for this. Not an exception when it cannot run: the
    // trim before it has already committed, so the reason comes back to be
    // reported beside what was removed.
    bool rebuild(QString& reason);

private:
    // A message's picture or voice note, by the reference the message itself
    // carries. Removing a message removes them, so they are read from the row
    // while the row is still there. False when a statement failed - what that
    // costs is the caller's to decide.
    bool removeMediaOfMessage(const QString& e2eId);
    // The same for every message of a conversation, and for the messages one
    // conversation is about to be trimmed of. Both run before those rows go.
    bool removeMediaOfPeer(const QString& peer);
    bool removeMediaOfTrimmed(const QString& peer, int keep);
    // Whether the core's media table is there at all. A store opened before the
    // account's own tables exist has no media, which is not a failure.
    bool hasMediaTable() const;
    // The rows of one conversation past the newest `keep`, and their reactions.
    // Runs inside the caller's transaction; throws on a failed statement.
    qint64 pruneRowsOf(const QString& peer, int keep);

    // The open connection. Owned; closed in the destructor.
    sqlite3* db_ = nullptr;
    // The file behind that connection, so its size can be measured and a rebuild
    // can check there is room for one.
    QString path_;
};

}  // namespace bazarish::app
