// Bazarish project (c) 2026
#pragma once

#include <QString>
#include <QVector>

#include <cstdint>

namespace bazarish::app {

// One stored conversation entry. Mirrors the rendered message; bodies live in
// a per-profile SQLite database under the profile directory.
struct StoredMessage {
    qint64 id = 0;
    QString peer;          // contact fingerprint
    bool outgoing = false;
    QString type;          // content type: text/file/photo/... or "system"
    QString protocolId;    // envelope message id (to match delivery receipts)
    QString text;
    QString attName;
    QString attMime;
    qint64 attSize = 0;
    QString attRef;        // content-store id
    QString attKey;        // base64 content key
    qint64 ts = 0;         // unix seconds
    int status = 0;        // 0 sending, 1 sent, 2 failed, 3 received
};

// Persistent local message log for one profile. NOTE: stored in the clear in
// the profile directory (like contacts.json); transcript at-rest encryption is
// a follow-up.
class TranscriptStore {
public:
    TranscriptStore();
    ~TranscriptStore();

    // Opens (and creates) the database for this profile id.
    bool open(const QString& profileId, const QString& dbPath);

    qint64 append(const StoredMessage& message);
    void updateStatus(qint64 id, int status);
    // The row id of an outgoing message with this protocol id (0 if none).
    qint64 idForProtocol(const QString& protocolId) const;
    QVector<StoredMessage> messagesFor(const QString& peer) const;
    QString lastText(const QString& peer) const;
    qint64 lastTime(const QString& peer) const;

private:
    QString connectionName_;
};

}  // namespace bazarish::app
