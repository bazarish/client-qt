// Bazarish project (c) 2026
#pragma once

#include <bazarish/Bytes.hpp>

#include <QByteArray>
#include <QMap>
#include <QString>
#include <QPair>
#include <QVariant>
#include <QVector>

#include <cstdint>
#include <optional>
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
    QString author;
    bool contactable = false;
    bool fromHost = false;
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

struct GroupState {
    QString kind;
    qint64 members = 0;
    bool write = false;
    bool admin = false;
    qint64 at = 0;
};

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

class TranscriptReader {
public:
    TranscriptReader();
    ~TranscriptReader();

    TranscriptReader(const TranscriptReader&) = delete;
    TranscriptReader& operator=(const TranscriptReader&) = delete;

    void close();
    bool isOpen() const { return db_ != nullptr; }
    bool open(const QString& dbPath, const Bytes& key);

    QString sourcePathFor(qint64 id) const;
    QByteArray media(const QString& key) const;
    qint64 idForE2e(const QString& e2eId) const;
    qint64 idForKey(const QString& peer, const QString& e2eId, bool outgoing) const;
    qint64 idForIncomingE2e(const QString& e2eId, const QString& peer) const;
    qint64 oldestOfType(const QString& peer, const QString& type, bool outgoing) const;
    qint64 idForAnyProtocol(const QString& e2eId, const QString& peer) const;
    StoredMessage messageByE2e(const QString& e2eId, const QString& peer) const;
    StoredMessage messageById(qint64 id) const;
    QVector<StoredMessage> messagesFor(const QString& peer) const;
    QVector<StoredMessage> latestMessages(const QString& peer, int limit) const;
    QVector<StoredMessage> olderMessages(const QString& peer, qint64 beforeId, int limit) const;
    QVector<StoredMessage> newerMessages(const QString& peer, qint64 afterId, int limit) const;
    QVector<StoredMessage> messagesAround(const QString& peer, qint64 id, int perSide) const;
    StoredMessage nextVoiceAfter(const QString& peer, qint64 afterId) const;
    StoredMessage pictureBeside(const QString& peer, qint64 id, bool later) const;
    bool hasMessagesBefore(const QString& peer, qint64 id) const;
    // The chat's auto-delete timer in seconds, 0 when it has none.
    qint64 chatTimer(const QString& peer) const;
    // The peer's bot menu as {title, command} maps, empty when it has none.
    QVariantList botMenu(const QString& peer) const;
    // Messages whose time ran out by then, as (peer, id), oldest first.
    QVector<QPair<QString, qint64>> expiredBy(qint64 nowMs) const;
    bool hasMessagesAfter(const QString& peer, qint64 id) const;
    QVector<SearchHit> searchInPeer(const QString& peer, const QString& query) const;
    struct LastMessage {
        QString text;
        QString type;
        QString attachment;
        QString author;
    };
    LastMessage lastMessage(const QString& peer) const;
    qint64 lastTime(const QString& peer) const;
    QStringList conversationPeers() const;
    qint64 lastReadId(const QString& peer) const;
    bool isPinned(const QString& peer) const;
    QStringList pinnedPeers() const;
    int unreadCount(const QString& peer) const;
    int unreadTotal() const;
    qint64 firstUnreadId(const QString& peer) const;
    QVector<Reaction> reactionsFor(const QString& peer, const QString& target) const;
    std::optional<GroupState> groupState(const QString& peer) const;
    QMap<QString, qint64> groupReactionCounts(const QString& peer, const QString& target) const;
    QVector<ChatWeight> chatWeights() const;
    DatabaseFootprint footprint() const;

protected:
    bool hasMediaTable() const;

    sqlite3* db_ = nullptr;
    QString path_;
};

class TranscriptStore : public TranscriptReader {
public:
    bool open(const QString& accountId, const QString& dbPath, const QString& passphrase = {});
    QString lastError() const { return lastError_; }

    [[nodiscard]] qint64 append(const StoredMessage& message);
    [[nodiscard]] bool updateStatus(qint64 id, int status);
    [[nodiscard]] std::optional<int> failUnsentOnLoad(
        int preparingStatus, int deliveringStatus, int failedStatus);
    [[nodiscard]] std::optional<int> settleUnfinishedNotes(const QString& type,
        int preparingStatus, int settledStatus, const QString& text);
    [[nodiscard]] bool markOutgoingReadUpTo(
        const QString& peer, qint64 uptoId, int readStatus, int minStatus, int maxStatus);
    [[nodiscard]] bool setSavedPath(qint64 id, const QString& path);
    [[nodiscard]] bool setHasPicture(qint64 id, bool has);
    [[nodiscard]] bool editContent(qint64 id, const QString& text, const QString& keyboard);
    [[nodiscard]] bool setType(qint64 id, const QString& type);
    [[nodiscard]] bool removeById(qint64 id);
    [[nodiscard]] bool clearPeer(const QString& peer);
    [[nodiscard]] bool forgetPeer(const QString& peer);
    [[nodiscard]] bool setLastReadId(const QString& peer, qint64 id);
    [[nodiscard]] bool applyReadThrough(const QString& peer, qint64 sentAtMs);
    [[nodiscard]] bool setPinned(const QString& peer, bool pinned);
    [[nodiscard]] bool setChatTimer(const QString& peer, qint64 seconds);
    [[nodiscard]] bool setBotMenu(const QString& peer, const QString& itemsJson);
    [[nodiscard]] bool setReaction(
        const QString& peer, const QString& target, const QString& reactor, const QString& emoji);
    [[nodiscard]] bool setGroupState(const QString& peer, const GroupState& state);
    [[nodiscard]] bool dropGroupState(const QString& peer);
    [[nodiscard]] bool setGroupReactionCounts(
        const QString& peer, const QString& target, const QMap<QString, qint64>& counts);
    qint64 pruneToLatest(const QString& peer, int keep);
    qint64 pruneEveryChatToLatest(int keep);
    bool rebuild(QString& reason);

private:
    bool failed();
    bool stampExpiry(qint64 id, const QString& peer, qint64 atMs);
    bool stampNewlyRead(const QString& peer, qint64 afterId, qint64 uptoId);
    bool clearPeerRows(const QString& peer);
    bool removeMediaOfMessage(const QString& e2eId);
    bool removeMediaOfPeer(const QString& peer);
    bool removeMediaOfTrimmed(const QString& peer, int keep);
    qint64 pruneRowsOf(const QString& peer, int keep);

    QString lastError_;
};

}  // namespace bazarish::app
