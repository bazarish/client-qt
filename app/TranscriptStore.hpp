// Bazarish project (c) 2026
#pragma once

#include <QByteArray>
#include <QString>
#include <QVector>

#include <cstdint>
#include <string>

struct sqlite3;

namespace bazarish::app {

struct StoredMessage {
    qint64 id = 0;
    QString peer;
    bool outgoing = false;
    QString type;
    QString e2eId;
    QString text;
    QString attName;
    QString attMime;
    qint64 attSize = 0;
    qint64 attDurationMs = 0;
    QString attWave;
    QString attRef;
    QString attSrcPath;
    QString savedPath;
    bool hasPicture = false;
    QString keyboard;
    QString replyTo;
    bool edited = false;
    bool forwarded = false;
    qint64 ts = 0;
    qint64 orderKey = 0;
    int status = 0;
};

struct SearchHit {
    qint64 id = 0;
    qint64 ts = 0;
    QString text;
    bool outgoing = false;
};

struct Reaction {
    QString reactor;
    QString emoji;
};

inline constexpr int kAccountSchemaVersion = 1;

inline constexpr int kKeepRecentMessages = 100;
inline constexpr int kKeepManyMessages = 1000;

struct ChatWeight {
    QString peer;
    qint64 messages = 0;
    qint64 rowBytes = 0;
    qint64 mediaBytes = 0;
    qint64 mediaCount = 0;
};

struct DatabaseFootprint {
    qint64 fileBytes = 0;
    qint64 freeBytes = 0;
};

class TranscriptStore {
public:
    TranscriptStore();
    ~TranscriptStore();

    void close();

    bool open(const QString& accountId, const QString& dbPath, const QString& passphrase = {});

    qint64 append(const StoredMessage& message);
    void updateStatus(qint64 id, int status);
    int failUnsentOnLoad(int preparingStatus, int deliveringStatus, int failedStatus);
    int settleUnfinishedNotes(const QString& type, int preparingStatus, int settledStatus,
        const QString& text);
    void markOutgoingReadUpTo(
        const QString& peer, qint64 uptoId, int readStatus, int minStatus, int maxStatus);
    QString sourcePathFor(qint64 id) const;
    void setSavedPath(qint64 id, const QString& path);
    QByteArray media(const QString& key) const;

    void setHasPicture(qint64 id, bool has);
    qint64 idForE2e(const QString& e2eId) const;
    qint64 idForKey(const QString& peer, const QString& e2eId, bool outgoing) const;
    qint64 idForIncomingE2e(const QString& e2eId, const QString& peer) const;
    qint64 oldestOfType(const QString& peer, const QString& type, bool outgoing) const;
    qint64 idForAnyProtocol(const QString& e2eId, const QString& peer) const;
    StoredMessage messageByE2e(const QString& e2eId, const QString& peer) const;
    void editContent(qint64 id, const QString& text, const QString& keyboard);
    void setType(qint64 id, const QString& type);
    void removeById(qint64 id);
    void clearPeer(const QString& peer);
    void forgetPeer(const QString& peer);
    QVector<StoredMessage> messagesFor(const QString& peer) const;
    QVector<StoredMessage> latestMessages(const QString& peer, int limit) const;
    QVector<StoredMessage> olderMessages(const QString& peer, qint64 beforeId, int limit) const;
    QVector<StoredMessage> newerMessages(const QString& peer, qint64 afterId, int limit) const;
    StoredMessage nextVoiceAfter(const QString& peer, qint64 afterId) const;
    bool hasMessagesBefore(const QString& peer, qint64 id) const;
    bool hasMessagesAfter(const QString& peer, qint64 id) const;
    QVector<SearchHit> searchInPeer(const QString& peer, const QString& query) const;
    struct LastMessage {
        QString text;
        QString type;
        QString attachment;
    };
    LastMessage lastMessage(const QString& peer) const;
    qint64 lastTime(const QString& peer) const;
    QStringList conversationPeers() const;

    void setLastReadId(const QString& peer, qint64 id);
    void applyReadThrough(const QString& peer, qint64 sentAtMs);
    qint64 lastReadId(const QString& peer) const;

    void setPinned(const QString& peer, bool pinned);
    bool isPinned(const QString& peer) const;
    QStringList pinnedPeers() const;
    int unreadCount(const QString& peer) const;
    qint64 firstUnreadId(const QString& peer) const;

    void setReaction(
        const QString& peer, const QString& target, const QString& reactor, const QString& emoji);
    QVector<Reaction> reactionsFor(const QString& peer, const QString& target) const;

    QVector<ChatWeight> chatWeights() const;
    DatabaseFootprint footprint() const;
    qint64 pruneToLatest(const QString& peer, int keep);
    qint64 pruneEveryChatToLatest(int keep);
    bool rebuild(QString& reason);

private:
    bool removeMediaOfMessage(const QString& e2eId);
    bool removeMediaOfPeer(const QString& peer);
    bool removeMediaOfTrimmed(const QString& peer, int keep);
    bool hasMediaTable() const;
    qint64 pruneRowsOf(const QString& peer, int keep);

    sqlite3* db_ = nullptr;
    QString path_;
};

}  // namespace bazarish::app
