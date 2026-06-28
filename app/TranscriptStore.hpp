// Bazarish project (c) 2026
#pragma once

#include <QString>
#include <QVector>

#include <cstdint>
#include <string>

namespace bazarish::app {

// One stored conversation entry. Mirrors the rendered message; bodies live in
// a per-profile SQLite database under the profile directory.
struct StoredMessage {
    qint64 id = 0;
    QString peer;          // contact fingerprint, or a group id for group messages
    bool outgoing = false;
    QString type;          // content type: text/file/photo/... or "system"
    QString sender;        // author fingerprint for an incoming group message
    QString protocolId;    // envelope message id (to match delivery receipts)
    QString text;
    QString attName;
    QString attMime;
    qint64 attSize = 0;
    QString attRef;        // content-store id
    QString attKey;        // base64 content key
    QString attSrcPath;    // local source path of an outgoing attachment (for resend)
    QString savedPath;     // where an incoming attachment was last saved (local path)
    bool blobGone = false; // incoming attachment whose blob is gone from the store
                           // (download returned 404/410); shows "Not found", no Save
    QString keyboard;      // inline-keyboard JSON (empty when none)
    QString replyTo;       // protocol id of the message this one replies to (empty
                           // when not a reply); the UI resolves it to a local row
    bool edited = false;   // true once the message was edited in place
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
    QString sender;
};

// One reaction on a message: who reacted (fingerprint) and the emoji. One per
// reactor per message - a new emoji from the same reactor overwrites the old.
struct Reaction {
    QString reactor;
    QString emoji;
};

// Persistent local message log for one profile. When a passphrase is given the
// database is never written to disk in the clear: it lives in an in-memory
// SQLite connection and is persisted as a single CMS PWRI-sealed blob
// (<dbPath>.enc), serialized/deserialized through the SQLite driver. With an
// empty passphrase it falls back to a plaintext file (unencrypted profiles).
class TranscriptStore {
public:
    TranscriptStore();
    ~TranscriptStore();

    // Opens (and creates) the database for this profile id. A non-empty
    // passphrase enables the sealed in-memory mode described above.
    bool open(const QString& profileId, const QString& dbPath, const QString& passphrase = {});

    qint64 append(const StoredMessage& message);
    void updateStatus(qint64 id, int status);
    // Marks every outgoing message still left at the "sending" status (there is no
    // persistent outbound queue, so on load these are interrupted sends, not ones
    // in flight) as failedStatus, so the UI shows "not sent" with a resend option
    // instead of a perpetual upload animation. Returns the number changed.
    int failUnsentOnLoad(int sendingStatus, int failedStatus);
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
    void setBlobGone(qint64 id, bool gone);
    // The row id of an outgoing message with this protocol id (0 if none).
    qint64 idForProtocol(const QString& protocolId) const;
    // The row id of an incoming message from peer with this protocol id, the
    // target of an edit (0 if none). Scoping to incoming-from-peer is the
    // security check: a peer can only edit a message it actually sent.
    qint64 idForIncomingProtocol(const QString& protocolId, const QString& peer) const;
    // The row id of an incoming group message under `peer` (a group id) with this
    // protocol id AND this author (0 if none). Scoping to the author is the group
    // edit/delete security check: a member can only edit a message it actually sent.
    qint64 idForIncomingGroupProtocol(
        const QString& protocolId, const QString& peer, const QString& sender) const;
    // The row id of a message under `peer` with this protocol id, either direction
    // (0 if none). Used to resolve a reply reference to a local message to jump to.
    qint64 idForAnyProtocol(const QString& protocolId, const QString& peer) const;
    // The full row for a protocol id under `peer`, either direction (id 0 when not
    // found). Used to render a reply quote (author + text/file name of the original).
    StoredMessage messageByProtocol(const QString& protocolId, const QString& peer) const;
    // Replaces a message's text and keyboard and marks it edited.
    void editContent(qint64 id, const QString& text, const QString& keyboard);
    // Permanently removes a message (delete with no trace).
    void removeById(qint64 id);
    // Permanently removes every message of a conversation (clear chat / a left
    // group / a deleted contact). The peer key is a contact fingerprint or group id.
    void clearPeer(const QString& peer);
    QVector<StoredMessage> messagesFor(const QString& peer) const;
    // Windowed reads for paging a large conversation: the newest `limit` rows,
    // the `limit` rows just older than beforeId, and the `limit` rows just newer
    // than afterId - all returned oldest-first (the display order).
    QVector<StoredMessage> latestMessages(const QString& peer, int limit) const;
    QVector<StoredMessage> olderMessages(const QString& peer, qint64 beforeId, int limit) const;
    QVector<StoredMessage> newerMessages(const QString& peer, qint64 afterId, int limit) const;
    // Whether any row exists strictly older / newer than id (drives "load more").
    bool hasMessagesBefore(const QString& peer, qint64 id) const;
    bool hasMessagesAfter(const QString& peer, qint64 id) const;
    // Case-insensitive full-text matches within a conversation, newest first.
    QVector<SearchHit> searchInPeer(const QString& peer, const QString& query) const;
    QString lastText(const QString& peer) const;
    qint64 lastTime(const QString& peer) const;

    // --- Read state (persistent unread tracking) ---
    // A per-peer high-water of the last locally-read incoming message id. It only
    // advances (a lower id is ignored), so re-reading older history never lowers it.
    // Survives restart, so the unread badge and the open-at-first-unread position
    // are accurate across sessions.
    void setLastReadId(const QString& peer, qint64 id);
    qint64 lastReadId(const QString& peer) const;
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

    // --- Group read receipts (who has viewed a message) ---
    // Records that `viewer` has read `target` under group `peer` (insert-or-ignore).
    void addView(const QString& peer, const QString& target, const QString& viewer);
    // Every member who has read `target`, for the viewers list and the green status.
    QStringList viewersFor(const QString& peer, const QString& target) const;

private:
    // Serializes the in-memory database and writes the sealed blob. No-op when
    // the store is not in encrypted mode.
    void flush() const;

    QString connectionName_;
    // Sealed-blob mode (non-empty passphrase): the at-rest file and the key.
    bool encrypted_ = false;
    QString blobPath_;
    std::string passphrase_;
    // True only after a fully successful open(), so a flush triggered while
    // tearing down a failed open never overwrites a good blob.
    bool ready_ = false;
};

}  // namespace bazarish::app
