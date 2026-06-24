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
    QString keyboard;      // inline-keyboard JSON (empty when none)
    bool edited = false;   // true once the message was edited in place
    qint64 ts = 0;         // unix seconds
    int status = 0;        // 0 sending, 1 sent, 2 failed, 3 received
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
    // The local source path recorded for an outgoing attachment (empty if none).
    QString sourcePathFor(qint64 id) const;
    // The row id of an outgoing message with this protocol id (0 if none).
    qint64 idForProtocol(const QString& protocolId) const;
    // The row id of an incoming message from peer with this protocol id, the
    // target of an edit (0 if none). Scoping to incoming-from-peer is the
    // security check: a peer can only edit a message it actually sent.
    qint64 idForIncomingProtocol(const QString& protocolId, const QString& peer) const;
    // Replaces a message's text and keyboard and marks it edited.
    void editContent(qint64 id, const QString& text, const QString& keyboard);
    QVector<StoredMessage> messagesFor(const QString& peer) const;
    QString lastText(const QString& peer) const;
    qint64 lastTime(const QString& peer) const;

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
