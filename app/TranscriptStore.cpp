// Bazarish project (c) 2026
#include "TranscriptStore.hpp"

#include "PathText.hpp"
#include "SessionShared.hpp"
#include "SystemNotes.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QStringList>

#include <string>
#include <utility>

namespace bazarish::app {

namespace {

std::string utf8(const QString& text)
{
    return text.toStdString();
}

QString qt(const std::string& text)
{
    return QString::fromStdString(text);
}

StoredMessage toQt(const client::StoredMessage& m)
{
    StoredMessage out;
    out.id = m.id;
    out.peer = qt(m.peer);
    out.outgoing = m.outgoing;
    out.type = qt(m.type);
    out.e2eId = qt(m.e2eId);
    out.text = qt(m.text);
    out.attName = qt(m.attName);
    out.attMime = qt(m.attMime);
    out.attSize = m.attSize;
    out.attDurationMs = m.attDurationMs;
    out.attWave = qt(m.attWave);
    out.attRef = qt(m.attRef);
    out.attSrcPath = qt(m.attSrcPath);
    out.savedPath = qt(m.savedPath);
    out.hasPicture = m.hasPicture;
    out.keyboard = qt(m.keyboard);
    out.replyTo = qt(m.replyTo);
    out.edited = m.edited;
    out.forwarded = m.forwarded;
    out.ts = m.ts;
    out.orderKey = m.orderKey;
    out.status = m.status;
    out.author = qt(m.author);
    out.contactable = m.contactable;
    out.fromHost = m.fromHost;
    return out;
}

client::StoredMessage toCore(const StoredMessage& m)
{
    client::StoredMessage out;
    out.id = m.id;
    out.peer = utf8(m.peer);
    out.outgoing = m.outgoing;
    out.type = utf8(m.type);
    out.e2eId = utf8(m.e2eId);
    out.text = utf8(m.text);
    out.attName = utf8(m.attName);
    out.attMime = utf8(m.attMime);
    out.attSize = m.attSize;
    out.attDurationMs = m.attDurationMs;
    out.attWave = utf8(m.attWave);
    out.attRef = utf8(m.attRef);
    out.attSrcPath = utf8(m.attSrcPath);
    out.savedPath = utf8(m.savedPath);
    out.hasPicture = m.hasPicture;
    out.keyboard = utf8(m.keyboard);
    out.replyTo = utf8(m.replyTo);
    out.edited = m.edited;
    out.forwarded = m.forwarded;
    out.ts = m.ts;
    out.orderKey = m.orderKey;
    out.status = m.status;
    out.author = utf8(m.author);
    out.contactable = m.contactable;
    out.fromHost = m.fromHost;
    return out;
}

QVector<StoredMessage> toQt(const std::vector<client::StoredMessage>& rows)
{
    QVector<StoredMessage> out;
    out.reserve(static_cast<qsizetype>(rows.size()));
    for (const client::StoredMessage& row : rows) {
        out.push_back(toQt(row));
    }
    return out;
}

}  // namespace

TranscriptReader::TranscriptReader()
    : TranscriptReader(std::make_unique<client::TranscriptReader>())
{
}

TranscriptReader::TranscriptReader(std::unique_ptr<client::TranscriptReader> core)
    : core_(std::move(core))
{
}

TranscriptReader::~TranscriptReader() = default;

void TranscriptReader::close()
{
    core_->close();
}

bool TranscriptReader::open(const QString& dbPath, const Bytes& key)
{
    return core_->open(toPath(dbPath), key);
}

QByteArray TranscriptReader::media(const QString& key) const
{
    return toQByteArray(core_->media(utf8(key)));
}

qint64 TranscriptReader::idForKey(const QString& peer, const QString& e2eId, const bool outgoing) const
{
    return core_->idForKey(utf8(peer), utf8(e2eId), outgoing);
}

qint64 TranscriptReader::oldestOfType(
    const QString& peer, const QString& type, const bool outgoing) const
{
    return core_->oldestOfType(utf8(peer), utf8(type), outgoing);
}

StoredMessage TranscriptReader::messageByE2e(const QString& e2eId, const QString& peer) const
{
    return toQt(core_->messageByE2e(utf8(e2eId), utf8(peer)));
}

StoredMessage TranscriptReader::messageById(const qint64 id) const
{
    return toQt(core_->messageById(id));
}

QVector<StoredMessage> TranscriptReader::latestMessages(const QString& peer, const int limit) const
{
    return toQt(core_->latestMessages(utf8(peer), limit));
}

QVector<StoredMessage> TranscriptReader::olderMessages(
    const QString& peer, const qint64 beforeId, const int limit) const
{
    return toQt(core_->olderMessages(utf8(peer), beforeId, limit));
}

QVector<StoredMessage> TranscriptReader::newerMessages(
    const QString& peer, const qint64 afterId, const int limit) const
{
    return toQt(core_->newerMessages(utf8(peer), afterId, limit));
}

QVector<StoredMessage> TranscriptReader::messagesAround(
    const QString& peer, const qint64 id, const int perSide) const
{
    return toQt(core_->messagesAround(utf8(peer), id, perSide));
}

StoredMessage TranscriptReader::nextVoiceAfter(const QString& peer, const qint64 afterId) const
{
    return toQt(core_->nextVoiceAfter(utf8(peer), afterId));
}

StoredMessage TranscriptReader::pictureBeside(
    const QString& peer, const qint64 id, const bool later) const
{
    return toQt(core_->pictureBeside(utf8(peer), id, later));
}

bool TranscriptReader::hasMessagesBefore(const QString& peer, const qint64 id) const
{
    return core_->hasMessagesBefore(utf8(peer), id);
}

bool TranscriptReader::hasMessagesAfter(const QString& peer, const qint64 id) const
{
    return core_->hasMessagesAfter(utf8(peer), id);
}

qint64 TranscriptReader::chatTimer(const QString& peer) const
{
    return core_->chatTimer(utf8(peer));
}

QVariantList TranscriptReader::botMenu(const QString& peer) const
{
    return QJsonDocument::fromJson(QByteArray::fromStdString(core_->botMenu(utf8(peer))))
        .array()
        .toVariantList();
}

QVector<QPair<QString, qint64>> TranscriptReader::expiredBy(const qint64 nowMs) const
{
    QVector<QPair<QString, qint64>> due;
    for (const auto& [peer, id] : core_->expiredBy(nowMs)) {
        due.append({qt(peer), id});
    }
    return due;
}

QVector<SearchHit> TranscriptReader::searchInPeer(const QString& peer, const QString& query) const
{
    QVector<SearchHit> hits;
    if (query.isEmpty()) {
        return hits;
    }
    constexpr int kMaxHits = 500;
    for (const client::SearchableRow& row : core_->searchableRows(utf8(peer))) {
        if (hits.size() >= kMaxHits) {
            break;
        }
        const QString stored = qt(row.text);
        const QString text = isServiceMessage(qt(row.type)) ? systemNoteText(stored) : stored;
        const QString attName = qt(row.attName);
        if (!text.contains(query, Qt::CaseInsensitive)
            && !attName.contains(query, Qt::CaseInsensitive)) {
            continue;
        }
        hits.push_back(
            {row.id, row.ts, !text.isEmpty() ? text : (QStringLiteral("📎 ") + attName), row.outgoing});
    }
    return hits;
}

TranscriptReader::LastMessage TranscriptReader::lastMessage(const QString& peer) const
{
    const client::LastMessage last = core_->lastMessage(utf8(peer));
    return {qt(last.text), qt(last.type), qt(last.attachment), qt(last.author)};
}

qint64 TranscriptReader::lastTime(const QString& peer) const
{
    return core_->lastTime(utf8(peer));
}

qint64 TranscriptReader::newestOwnOrderKey(const QString& peer) const
{
    return core_->newestOwnOrderKey(utf8(peer));
}

QStringList TranscriptReader::conversationPeers() const
{
    QStringList peers;
    for (const std::string& peer : core_->conversationPeers()) {
        peers << qt(peer);
    }
    return peers;
}

qint64 TranscriptReader::lastReadId(const QString& peer) const
{
    return core_->lastReadId(utf8(peer));
}

bool TranscriptReader::isPinned(const QString& peer) const
{
    return core_->isPinned(utf8(peer));
}

int TranscriptReader::unreadCount(const QString& peer) const
{
    return core_->unreadCount(utf8(peer));
}

int TranscriptReader::unreadTotal() const
{
    return core_->unreadTotal();
}

qint64 TranscriptReader::firstUnreadId(const QString& peer) const
{
    return core_->firstUnreadId(utf8(peer));
}

QVector<Reaction> TranscriptReader::reactionsFor(const QString& peer, const QString& target) const
{
    QVector<Reaction> out;
    for (const client::Reaction& reaction : core_->reactionsFor(utf8(peer), utf8(target))) {
        out.push_back({qt(reaction.reactor), qt(reaction.emoji)});
    }
    return out;
}

std::optional<GroupState> TranscriptReader::groupState(const QString& peer) const
{
    const std::optional<client::GroupState> state = core_->groupState(utf8(peer));
    if (!state) {
        return std::nullopt;
    }
    return GroupState{qt(state->kind), state->members, state->write, state->admin, state->at};
}

QMap<QString, qint64> TranscriptReader::groupReactionCounts(
    const QString& peer, const QString& target) const
{
    QMap<QString, qint64> out;
    for (const auto& [emoji, count] : core_->groupReactionCounts(utf8(peer), utf8(target))) {
        out.insert(qt(emoji), count);
    }
    return out;
}

QVector<ChatWeight> TranscriptReader::chatWeights() const
{
    QVector<ChatWeight> out;
    for (const client::ChatWeight& weight : core_->chatWeights()) {
        out.push_back({qt(weight.peer), weight.messages, weight.rowBytes, weight.mediaBytes,
            weight.mediaCount});
    }
    return out;
}

DatabaseFootprint TranscriptReader::footprint() const
{
    const client::DatabaseFootprint footprint = core_->footprint();
    return {footprint.fileBytes, footprint.freeBytes};
}

TranscriptStore::TranscriptStore()
    : TranscriptReader(std::make_unique<client::TranscriptStore>())
{
}

client::TranscriptStore& TranscriptStore::store()
{
    return static_cast<client::TranscriptStore&>(*core_);
}

QString TranscriptStore::lastError() const
{
    return qt(static_cast<const client::TranscriptStore&>(*core_).lastError());
}

bool TranscriptStore::open(const QString& dbPath, const QString& passphrase)
{
    return store().open(toPath(dbPath), utf8(passphrase));
}

qint64 TranscriptStore::append(const StoredMessage& message)
{
    return store().append(toCore(message));
}

bool TranscriptStore::updateStatus(const qint64 id, const int status)
{
    return store().updateStatus(id, status);
}

std::optional<int> TranscriptStore::settleUnfinishedNotes(const QString& type,
    const int preparingStatus, const int settledStatus, const QString& text)
{
    return store().settleUnfinishedNotes(utf8(type), preparingStatus, settledStatus, utf8(text));
}

bool TranscriptStore::markOutgoingReadUpTo(const QString& peer, const qint64 uptoId,
    const int readStatus, const int minStatus, const int maxStatus)
{
    return store().markOutgoingReadUpTo(utf8(peer), uptoId, readStatus, minStatus, maxStatus);
}

bool TranscriptStore::setSavedPath(const qint64 id, const QString& path)
{
    return store().setSavedPath(id, utf8(path));
}

bool TranscriptStore::setHasPicture(const qint64 id, const bool has)
{
    return store().setHasPicture(id, has);
}

bool TranscriptStore::keepMedia(const QString& key, const QByteArray& bytes)
{
    return store().keepMedia(utf8(key), Bytes(bytes.begin(), bytes.end()));
}

bool TranscriptStore::editContent(const qint64 id, const QString& text, const QString& keyboard)
{
    return store().editContent(id, utf8(text), utf8(keyboard));
}

bool TranscriptStore::setType(const qint64 id, const QString& type)
{
    return store().setType(id, utf8(type));
}

bool TranscriptStore::removeById(const qint64 id)
{
    return store().removeById(id);
}

bool TranscriptStore::clearPeer(const QString& peer)
{
    return store().clearPeer(utf8(peer));
}

bool TranscriptStore::forgetPeer(const QString& peer)
{
    return store().forgetPeer(utf8(peer));
}

bool TranscriptStore::setLastReadId(const QString& peer, const qint64 id)
{
    return store().setLastReadId(utf8(peer), id);
}

bool TranscriptStore::applyReadThrough(const QString& peer, const qint64 sentAtMs)
{
    return store().applyReadThrough(utf8(peer), sentAtMs);
}

bool TranscriptStore::setPinned(const QString& peer, const bool pinned)
{
    return store().setPinned(utf8(peer), pinned);
}

bool TranscriptStore::setChatTimer(const QString& peer, const qint64 seconds)
{
    return store().setChatTimer(utf8(peer), seconds);
}

bool TranscriptStore::setBotMenu(const QString& peer, const QString& itemsJson)
{
    return store().setBotMenu(utf8(peer), utf8(itemsJson));
}

bool TranscriptStore::setReaction(
    const QString& peer, const QString& target, const QString& reactor, const QString& emoji)
{
    return store().setReaction(utf8(peer), utf8(target), utf8(reactor), utf8(emoji));
}

bool TranscriptStore::setGroupState(const QString& peer, const GroupState& state)
{
    return store().setGroupState(utf8(peer), {utf8(state.kind), state.members, state.write, state.admin, state.at});
}

bool TranscriptStore::dropGroupState(const QString& peer)
{
    return store().dropGroupState(utf8(peer));
}

bool TranscriptStore::setGroupReactionCounts(
    const QString& peer, const QString& target, const QMap<QString, qint64>& counts)
{
    std::map<std::string, std::int64_t> core;
    for (auto it = counts.cbegin(); it != counts.cend(); ++it) {
        core[utf8(it.key())] = it.value();
    }
    return store().setGroupReactionCounts(utf8(peer), utf8(target), core);
}

qint64 TranscriptStore::pruneToLatest(const QString& peer, const int keep)
{
    return store().pruneToLatest(utf8(peer), keep);
}

qint64 TranscriptStore::pruneEveryChatToLatest(const int keep)
{
    return store().pruneEveryChatToLatest(keep);
}

bool TranscriptStore::rebuild(QString& reason)
{
    std::string why;
    const bool rebuilt = store().rebuild(why);
    reason = qt(why);
    return rebuilt;
}

}  // namespace bazarish::app
