// Bazarish project (c) 2026
#include "Models.hpp"

#include "DeliveryStatus.hpp"
#include "SystemNotes.hpp"

#include <QDateTime>
#include <QSet>

#include <algorithm>

namespace bazarish::app {

int AccountListModel::rowCount(const QModelIndex&) const
{
    return static_cast<int>(accounts_.size());
}

QVariant AccountListModel::data(const QModelIndex& index, int role) const
{
    if (index.row() < 0 || index.row() >= accounts_.size()) {
        return {};
    }
    const AccountListRow& p = accounts_[index.row()];
    switch (role) {
    case IdRole: return p.id;
    case NameRole: return p.name;
    case FingerprintRole: return p.fingerprint;
    case EncryptedRole: return p.encrypted;
    case OpenRole: return p.open;
    case PrimaryRole: return p.primary;
    case OtherPrimariesRole: return p.otherPrimaries;
    default: return {};
    }
}

QHash<int, QByteArray> AccountListModel::roleNames() const
{
    return {{IdRole, "accountId"}, {NameRole, "name"}, {FingerprintRole, "fingerprint"},
        {EncryptedRole, "encrypted"}, {OpenRole, "open"}, {PrimaryRole, "primary"},
        {OtherPrimariesRole, "otherPrimaries"}};
}

void AccountListModel::setAccounts(QVector<AccountListRow> accounts)
{
    beginResetModel();
    accounts_ = std::move(accounts);
    endResetModel();
}

int ContactListModel::rowCount(const QModelIndex&) const
{
    return static_cast<int>(contacts_.size());
}

QVariant ContactListModel::data(const QModelIndex& index, int role) const
{
    if (index.row() < 0 || index.row() >= contacts_.size()) {
        return {};
    }
    const ContactRow& c = contacts_[index.row()];
    switch (role) {
    case FingerprintRole: return c.fingerprint;
    case NameRole: return c.name;
    case LastTextRole: return c.lastText;
    case LastTimeRole: return c.lastTime;
    case UnreadRole: return c.unread;
    case MentionsRole: return c.mentions;
    case PinnedRole: return c.pinned;
    case SavedRole: return c.saved;
    default: return {};
    }
}

QHash<int, QByteArray> ContactListModel::roleNames() const
{
    return {{SavedRole, "saved"}, {FingerprintRole, "fingerprint"}, {NameRole, "name"},
        {LastTextRole, "lastText"},
        {LastTimeRole, "lastTime"}, {UnreadRole, "unread"},
        {PinnedRole, "pinned"}, {MentionsRole, "mentions"}};
}

namespace {

bool before(const ContactRow& a, const ContactRow& b)
{
    if (a.pinned != b.pinned) {
        return a.pinned;
    }
    return a.lastTime > b.lastTime;
}

}  // namespace

void ContactListModel::setContacts(QVector<ContactRow> contacts)
{
    beginResetModel();
    contacts_ = std::move(contacts);
    std::stable_sort(contacts_.begin(), contacts_.end(), before);
    endResetModel();
}

void ContactListModel::remove(const QString& fingerprint)
{
    const int at = indexOf(fingerprint);
    if (at < 0) {
        return;
    }
    beginRemoveRows({}, at, at);
    contacts_.remove(at);
    endRemoveRows();
}

int ContactListModel::indexOf(const QString& fingerprint) const
{
    for (int i = 0; i < contacts_.size(); ++i) {
        if (contacts_[i].fingerprint == fingerprint) {
            return i;
        }
    }
    return -1;
}

void ContactListModel::resort()
{
    beginResetModel();
    std::stable_sort(contacts_.begin(), contacts_.end(), before);
    endResetModel();
}

void ContactListModel::touch(const QString& fingerprint, const QString& name,
    const QString& lastText, qint64 lastTime)
{
    const int i = indexOf(fingerprint);
    if (i < 0) {
        beginInsertRows({}, 0, 0);
        contacts_.prepend(
            ContactRow{fingerprint, name.isEmpty() ? fingerprint : name, lastText, lastTime});
        endInsertRows();
        resort();
        return;
    }
    ContactRow& c = contacts_[i];
    if (!name.isEmpty()) {
        c.name = name;
    }
    c.lastText = lastText;
    c.lastTime = lastTime;
    resort();
}

void ContactListModel::setUnread(const QString& fingerprint, const int count, const int mentions)
{
    const int i = indexOf(fingerprint);
    if (i >= 0 && (contacts_[i].unread != count || contacts_[i].mentions != mentions)) {
        contacts_[i].unread = count;
        contacts_[i].mentions = mentions;
        const QModelIndex idx = index(i);
        emit dataChanged(idx, idx, {UnreadRole, MentionsRole});
    }
}

int ContactListModel::totalUnread(const QString& except) const
{
    int total = 0;
    for (const ContactRow& c : contacts_) {
        if (c.fingerprint != except) {
            total += c.unread;
        }
    }
    return total;
}

int ConversationModel::rowCount(const QModelIndex&) const
{
    return static_cast<int>(messages_.size());
}

QVariant ConversationModel::data(const QModelIndex& index, int role) const
{
    if (index.row() < 0 || index.row() >= messages_.size()) {
        return {};
    }
    const StoredMessage& m = messages_[index.row()];
    switch (role) {
    case OutgoingRole: return m.outgoing;
    case TypeRole: return m.type;
    case TextRole: return isServiceMessage(m.type) ? systemNoteText(m.text) : m.text;
    case AttNameRole: return m.attName;
    case AttMimeRole: return m.attMime;
    case AttSizeRole: return m.attSize;
    case AttRefRole: return m.attRef;
    case KeyboardRole: return m.keyboard;
    case E2eIdRole: return m.e2eId;
    case EditedRole: return m.edited;
    case ForwardedRole: return m.forwarded;
    case TimeRole: return m.ts;
    case StatusRole: return m.status;
    case MsgIdRole: return m.id;
    case ErrorRole: return live_.value(m.id).error;
    case UploadProgressRole: return live_.value(m.id).uploadProgress;
    case DownloadingRole: return live_.value(m.id).downloading;
    case DownloadReceivedRole: return live_.value(m.id).downloadReceived;
    case DownloadTotalRole: return live_.value(m.id).downloadTotal;
    case DownloadErrorRole: return live_.value(m.id).downloadError;
    case TransferStageRole: return live_.value(m.id).transferStage;
    case SavedPathRole: return m.savedPath;
    case PictureRole: return m.hasPicture;
    case DurationRole: return m.attDurationMs;
    case WaveRole: return m.attWave;
    case ReplyToRole: return m.replyTo;
    case AuthorRole: return m.author;
    case ContactableRole: return m.contactable;
    case FromHostRole: return m.fromHost;
    case MentionsMeRole: return m.mentionsMe;
    case DeviceRole: return m.device;
    case ReactionsRole: return reactions_.value(m.e2eId).summary;
    case FreshReactionsRole: return reactions_.value(m.e2eId).fresh;
    case PictureVersionRole: return pictureVersions_.value(m.e2eId);
    case DayRole:
        return m.ts > 0
            ? QDateTime::fromMSecsSinceEpoch(m.ts).date().toString(QStringLiteral("yyyy-MM-dd"))
            : QString();
    default: return {};
    }
}

QHash<int, QByteArray> ConversationModel::roleNames() const
{
    return {{OutgoingRole, "outgoing"}, {TypeRole, "type"}, {TextRole, "text"},
        {AttNameRole, "attName"}, {AttMimeRole, "attMime"}, {AttSizeRole, "attSize"},
        {AttRefRole, "attRef"}, {KeyboardRole, "keyboard"},
        {E2eIdRole, "e2eId"}, {EditedRole, "edited"}, {ForwardedRole, "forwarded"},
        {TimeRole, "time"}, {StatusRole, "status"}, {MsgIdRole, "msgId"}, {ErrorRole, "error"},
        {UploadProgressRole, "uploadProgress"}, {DayRole, "day"},
        {DownloadingRole, "downloading"}, {DownloadReceivedRole, "downloadReceived"},
        {DownloadTotalRole, "downloadTotal"}, {DownloadErrorRole, "downloadError"},
        {SavedPathRole, "savedPath"}, {PictureRole, "hasPicture"},
        {DurationRole, "attDurationMs"}, {WaveRole, "attWave"},
        {TransferStageRole, "transferStage"},
        {ReplyToRole, "replyTo"}, {AuthorRole, "author"}, {ContactableRole, "contactable"},
        {FromHostRole, "fromHost"}, {DeviceRole, "device"}, {ReactionsRole, "reactions"},
        {FreshReactionsRole, "freshReactions"}, {PictureVersionRole, "pictureVersion"},
        {MentionsMeRole, "mentionsMe"}};
}

void ConversationModel::setMessages(QVector<StoredMessage> messages, const ViewLanding landing)
{
    beginResetModel();
    messages_ = std::move(messages);
    live_.clear();
    pictureVersions_.clear();
    const QStringList ids = loadedE2eIds();
    const QSet<QString> loaded(ids.cbegin(), ids.cend());
    reactions_.removeIf([&loaded](const auto& entry) { return !loaded.contains(entry.key()); });
    landing_ = landing;
    endResetModel();
}

void ConversationModel::setLanding(const ViewLanding landing)
{
    landing_ = landing;
}

QVariantMap ConversationModel::landing() const
{
    const int count = static_cast<int>(messages_.size());
    int row = -1;
    if (landing_.mode == ViewLanding::eRow && count > 0) {
        row = static_cast<int>(std::clamp<qint64>(landing_.at, 0, count - 1));
    } else if (landing_.mode == ViewLanding::eUnread || landing_.mode == ViewLanding::eMessage) {
        row = rowForId(landing_.at);
    }
    const bool found = row >= 0;
    return {{QStringLiteral("row"), row},
        {QStringLiteral("centre"), found && landing_.mode == ViewLanding::eMessage},
        {QStringLiteral("unreadFrom"),
            found && landing_.mode == ViewLanding::eUnread ? landing_.at : qint64(-1)},
        {QStringLiteral("highlight"),
            found && landing_.mode == ViewLanding::eMessage ? landing_.at : qint64(-1)}};
}

void ConversationModel::putReactions(const QString& e2eId, const RowReactions& reactions)
{
    const auto known = reactions_.constFind(e2eId);
    if (known != reactions_.constEnd() && *known == reactions) {
        return;
    }
    reactions_.insert(e2eId, reactions);
    notifyRow(rowForE2e(e2eId), {ReactionsRole, FreshReactionsRole});
}

void ConversationModel::notePictureChanged(const QString& e2eId)
{
    const int row = rowForE2e(e2eId);
    if (row < 0) {
        return;
    }
    ++pictureVersions_[e2eId];
    notifyRow(row, {PictureVersionRole});
}

int ConversationModel::rowForE2e(const QString& e2eId) const
{
    if (e2eId.isEmpty()) {
        return -1;
    }
    for (int i = 0; i < messages_.size(); ++i) {
        if (messages_[i].e2eId == e2eId) {
            return i;
        }
    }
    return -1;
}

QStringList ConversationModel::loadedE2eIds() const
{
    QStringList ids;
    for (const StoredMessage& message : messages_) {
        if (!message.e2eId.isEmpty()) {
            ids << message.e2eId;
        }
    }
    return ids;
}

void ConversationModel::retranslate()
{
    if (messages_.isEmpty()) {
        return;
    }
    emit dataChanged(index(0), index(messages_.size() - 1), {TextRole});
}

int ConversationModel::appendMessage(const StoredMessage& message)
{
    // The core's notice of a row can land after a page that already brought it.
    if (const int known = rowForId(message.id); known >= 0) {
        messages_[known] = message;
        notifyRow(known, {});
        return known;
    }
    int row = static_cast<int>(messages_.size());
    while (row > 0) {
        const StoredMessage& prev = messages_[row - 1];
        const bool prevComesAfter = prev.orderKey > message.orderKey
            || (prev.orderKey == message.orderKey && prev.id > message.id);
        if (!prevComesAfter) {
            break;
        }
        --row;
    }
    beginInsertRows({}, row, row);
    messages_.insert(row, message);
    endInsertRows();
    return row;
}

QVector<StoredMessage> ConversationModel::unknownOf(const QVector<StoredMessage>& messages) const
{
    QVector<StoredMessage> unknown;
    for (const StoredMessage& message : messages) {
        if (rowForId(message.id) < 0) {
            unknown.push_back(message);
        }
    }
    return unknown;
}

void ConversationModel::prependMessages(const QVector<StoredMessage>& messages)
{
    QVector<StoredMessage> merged = unknownOf(messages);
    if (merged.isEmpty()) {
        return;
    }
    beginInsertRows({}, 0, static_cast<int>(merged.size()) - 1);
    merged += messages_;
    messages_ = std::move(merged);
    endInsertRows();
}

void ConversationModel::appendMessages(const QVector<StoredMessage>& messages)
{
    const QVector<StoredMessage> fresh = unknownOf(messages);
    if (fresh.isEmpty()) {
        return;
    }
    const int row = static_cast<int>(messages_.size());
    beginInsertRows({}, row, row + static_cast<int>(fresh.size()) - 1);
    messages_ += fresh;
    endInsertRows();
}

int ConversationModel::rowForId(qint64 id) const
{
    for (int i = 0; i < messages_.size(); ++i) {
        if (messages_[i].id == id) {
            return i;
        }
    }
    return -1;
}

void ConversationModel::notifyRow(const int row, const QList<int>& roles)
{
    if (row < 0) {
        return;
    }
    const QModelIndex at = index(row);
    emit dataChanged(at, at, roles);
}

void ConversationModel::markDeliveredThrough(qint64 uptoId)
{
    for (int i = 0; i < messages_.size(); ++i) {
        StoredMessage& m = messages_[i];
        if (m.outgoing && m.id <= uptoId && m.status == DeliveryStatus::AtRecipientServer) {
            m.status = DeliveryStatus::Delivered;
            const QModelIndex idx = index(i);
            emit dataChanged(idx, idx, {StatusRole});
        }
    }
}

bool ConversationModel::newestIncomingThrough(
    int row, qint64& outId, QString& outProtocol, qint64& outSentAt) const
{
    const int start = std::min(row, static_cast<int>(messages_.size()) - 1);
    for (int i = start; i >= 0; --i) {
        const StoredMessage& m = messages_[i];
        if (!m.outgoing && !m.e2eId.isEmpty() && m.type != "system") {
            outId = m.id;
            outProtocol = m.e2eId;
            outSentAt = m.ts;
            return true;
        }
    }
    return false;
}

void ConversationModel::setErrorForId(qint64 id, const QString& error)
{
    live_[id].error = error;
    notifyRow(rowForId(id), {ErrorRole});
}

void ConversationModel::setUploadProgressForId(qint64 id, double fraction)
{
    live_[id].uploadProgress = fraction;
    notifyRow(rowForId(id), {UploadProgressRole});
}

void ConversationModel::setDownloadProgressForId(qint64 id, qint64 received, qint64 total)
{
    LiveMessageState& state = live_[id];
    state.downloading = true;
    state.downloadReceived = received;
    state.downloadTotal = total;
    state.downloadError.clear();
    notifyRow(rowForId(id),
        {DownloadingRole, DownloadReceivedRole, DownloadTotalRole, DownloadErrorRole});
}

void ConversationModel::setTransferStageForId(const qint64 id, const QString& stage)
{
    live_[id].transferStage = stage;
    notifyRow(rowForId(id), {TransferStageRole});
}

void ConversationModel::finishDownloadForId(qint64 id, bool ok, const QString& error)
{
    LiveMessageState& state = live_[id];
    state.downloading = false;
    state.downloadReceived = 0;
    state.downloadTotal = 0;
    state.transferStage.clear();
    state.downloadError = (ok || error.isEmpty()) ? QString() : error;
    notifyRow(rowForId(id),
        {DownloadingRole, DownloadReceivedRole, DownloadTotalRole, DownloadErrorRole});
}

void ConversationModel::markPictureMissingForId(qint64 id)
{
    const int row = rowForId(id);
    if (row < 0) {
        return;
    }
    messages_[row].hasPicture = false;
    notifyRow(row, {PictureRole});
}

void ConversationModel::replaceById(const StoredMessage& message)
{
    const int row = rowForId(message.id);
    if (row < 0) {
        return;
    }
    messages_[row] = message;
    notifyRow(row, {});
}

void ConversationModel::removeById(qint64 id)
{
    const int row = rowForId(id);
    if (row < 0) {
        return;
    }
    beginRemoveRows({}, row, row);
    messages_.removeAt(row);
    endRemoveRows();
    live_.remove(id);
}

bool ConversationModel::lastMessageOutgoing() const
{
    return !messages_.isEmpty() && messages_.last().outgoing;
}

int OpenAccountsModel::rowCount(const QModelIndex&) const
{
    return static_cast<int>(accounts_.size());
}

QVariant OpenAccountsModel::data(const QModelIndex& index, int role) const
{
    if (index.row() < 0 || index.row() >= accounts_.size()) {
        return {};
    }
    const AccountRow& a = accounts_[index.row()];
    switch (role) {
    case IdRole: return a.id;
    case NameRole: return a.name;
    case FingerprintRole: return a.fingerprint;
    case OpenRole: return a.open;
    case ActiveRole: return a.active;
    case OnlineRole: return a.online;
    case ConnectedRole: return a.connected;
    case EncryptedRole: return a.encrypted;
    case UnreadRole: return a.unread;
    case ActiveFacadeRole: return a.activeFacade;
    case PrimaryRole: return a.primary;
    case OtherPrimariesRole: return a.otherPrimaries;
    default: return {};
    }
}

QHash<int, QByteArray> OpenAccountsModel::roleNames() const
{
    return {{IdRole, "accountId"}, {NameRole, "name"}, {FingerprintRole, "fingerprint"},
        {OpenRole, "open"}, {ActiveRole, "active"}, {OnlineRole, "online"},
        {ConnectedRole, "connected"}, {EncryptedRole, "encrypted"}, {UnreadRole, "unread"},
        {ActiveFacadeRole, "activeFacade"}, {PrimaryRole, "primary"},
        {OtherPrimariesRole, "otherPrimaries"}};
}

void OpenAccountsModel::setAccounts(QVector<AccountRow> accounts)
{
    beginResetModel();
    accounts_ = std::move(accounts);
    endResetModel();
}

int OperationListModel::rowCount(const QModelIndex&) const
{
    return static_cast<int>(ops_.size());
}

QVariant OperationListModel::data(const QModelIndex& index, int role) const
{
    if (index.row() < 0 || index.row() >= ops_.size()) {
        return {};
    }
    const OperationRow& o = ops_[index.row()];
    switch (role) {
    case OpIdRole: return o.id;
    case KindRole: return o.kind;
    case TitleRole: return o.title;
    case StatusRole: return o.status;
    case DetailRole: return o.detail;
    case ProgressRole: return o.progress;
    case StateRole: return o.state;
    case StartedAtRole: return o.startedAt;
    case PeerRole: return o.peer;
    case CancelIdRole: return o.cancelId;
    default: return {};
    }
}

QHash<int, QByteArray> OperationListModel::roleNames() const
{
    return {{OpIdRole, "opId"}, {KindRole, "kind"}, {TitleRole, "title"}, {StatusRole, "status"},
        {DetailRole, "detail"}, {ProgressRole, "progress"}, {StateRole, "state"},
        {StartedAtRole, "startedAt"}, {PeerRole, "peer"}, {CancelIdRole, "cancelId"}};
}

int OperationListModel::indexOf(const QString& id) const
{
    for (int i = 0; i < ops_.size(); ++i) {
        if (ops_[i].id == id) {
            return i;
        }
    }
    return -1;
}

void OperationListModel::upsert(const OperationRow& row)
{
    const int i = indexOf(row.id);
    if (i < 0) {
        beginInsertRows({}, 0, 0);
        ops_.prepend(row);
        endInsertRows();
        return;
    }
    ops_[i] = row;
    const QModelIndex idx = index(i);
    emit dataChanged(idx, idx);
}

void OperationListModel::remove(const QString& id)
{
    const int i = indexOf(id);
    if (i < 0) {
        return;
    }
    beginRemoveRows({}, i, i);
    ops_.removeAt(i);
    endRemoveRows();
}

int OperationListModel::runningCount() const
{
    int n = 0;
    for (const OperationRow& o : ops_) {
        if (o.state == eOpRunning) {
            ++n;
        }
    }
    return n;
}

}  // namespace bazarish::app
