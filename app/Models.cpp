// Bazarish project (c) 2026
#include "Models.hpp"

#include "DeliveryStatus.hpp"

#include <QDateTime>

#include <algorithm>

namespace bazarish::app {

// ---------------- AccountListModel ----------------

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
    case ConnectedRole: return p.connected;
    default: return {};
    }
}

QHash<int, QByteArray> AccountListModel::roleNames() const
{
    return {{IdRole, "accountId"}, {NameRole, "name"}, {FingerprintRole, "fingerprint"},
        {EncryptedRole, "encrypted"}, {ConnectedRole, "connected"}};
}

void AccountListModel::setAccounts(QVector<AccountListRow> accounts)
{
    beginResetModel();
    accounts_ = std::move(accounts);
    endResetModel();
}

// ---------------- ContactListModel ----------------

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
    case PinnedRole: return c.pinned;
    default: return {};
    }
}

QHash<int, QByteArray> ContactListModel::roleNames() const
{
    return {{FingerprintRole, "fingerprint"}, {NameRole, "name"}, {LastTextRole, "lastText"},
        {LastTimeRole, "lastTime"}, {UnreadRole, "unread"},
        {PinnedRole, "pinned"}};
}

void ContactListModel::setContacts(QVector<ContactRow> contacts)
{
    beginResetModel();
    contacts_ = std::move(contacts);
    std::stable_sort(contacts_.begin(), contacts_.end(),
        [](const ContactRow& a, const ContactRow& b) {
            // Pinned chats first, then by most-recent activity.
            if (a.pinned != b.pinned) {
                return a.pinned;
            }
            return a.lastTime > b.lastTime;
        });
    endResetModel();
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
    std::stable_sort(contacts_.begin(), contacts_.end(),
        [](const ContactRow& a, const ContactRow& b) {
            // Pinned chats first, then by most-recent activity.
            if (a.pinned != b.pinned) {
                return a.pinned;
            }
            return a.lastTime > b.lastTime;
        });
    endResetModel();
}

void ContactListModel::touch(const QString& fingerprint, const QString& name,
    const QString& lastText, qint64 lastTime, bool incrementUnread)
{
    const int i = indexOf(fingerprint);
    if (i < 0) {
        beginInsertRows({}, 0, 0);
        contacts_.prepend(ContactRow{fingerprint, name.isEmpty() ? fingerprint : name, lastText,
            lastTime, incrementUnread ? 1 : 0});
        endInsertRows();
        resort();
        return;
    }
    ContactRow& c = contacts_[i];
    if (!name.isEmpty()) {
        c.name = name;
    }
    if (!lastText.isEmpty()) {
        c.lastText = lastText;
    }
    if (lastTime > 0) {
        c.lastTime = lastTime;
    }
    if (incrementUnread) {
        ++c.unread;
    }
    resort();
}

void ContactListModel::setUnread(const QString& fingerprint, int count)
{
    const int i = indexOf(fingerprint);
    if (i >= 0 && contacts_[i].unread != count) {
        contacts_[i].unread = count;
        const QModelIndex idx = index(i);
        emit dataChanged(idx, idx, {UnreadRole});
    }
}

int ContactListModel::totalUnread() const
{
    int total = 0;
    for (const ContactRow& c : contacts_) {
        total += c.unread;
    }
    return total;
}

// ---------------- ConversationModel ----------------

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
    case TextRole: return m.text;
    case AttNameRole: return m.attName;
    case AttMimeRole: return m.attMime;
    case AttSizeRole: return m.attSize;
    case AttRefRole: return m.attRef;
    case AttKeyRole: return m.attKey;
    case KeyboardRole: return m.keyboard;
    case E2eIdRole: return m.e2eId;
    case EditedRole: return m.edited;
    case TimeRole: return m.ts;
    case StatusRole: return m.status;
    case MsgIdRole: return m.id;
    case ErrorRole: return errorById_.value(m.id);
    case UploadProgressRole: return uploadProgressById_.value(m.id, -1.0);
    case DownloadingRole: return downloadReceivedById_.contains(m.id);
    case DownloadReceivedRole: return downloadReceivedById_.value(m.id, 0);
    case DownloadTotalRole: return downloadTotalById_.value(m.id, 0);
    case DownloadErrorRole: return downloadErrorById_.value(m.id);
    case DownloadStageRole: return downloadStageById_.value(m.id, 0);
    case TransferStageRole: return transferStageById_.value(m.id);
    case SavedPathRole: return m.savedPath;
    case BlobGoneRole: return m.blobGone;
    case PictureRole: return m.hasPicture;
    case DurationRole: return m.attDurationMs;
    case WaveRole: return m.attWave;
    case ReplyToRole: return m.replyTo;
    // The local calendar day this message belongs to, as an ISO date string. The
    // view groups messages into per-day sections off this role and renders a
    // centered date separator at each change.
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
        {AttRefRole, "attRef"}, {AttKeyRole, "attKey"}, {KeyboardRole, "keyboard"},
        {E2eIdRole, "e2eId"}, {EditedRole, "edited"},
        {TimeRole, "time"}, {StatusRole, "status"}, {MsgIdRole, "msgId"}, {ErrorRole, "error"},
        {UploadProgressRole, "uploadProgress"}, {DayRole, "day"},
        {DownloadingRole, "downloading"}, {DownloadReceivedRole, "downloadReceived"},
        {DownloadTotalRole, "downloadTotal"}, {DownloadErrorRole, "downloadError"},
        {SavedPathRole, "savedPath"}, {BlobGoneRole, "blobGone"}, {PictureRole, "hasPicture"},
        {DurationRole, "attDurationMs"}, {WaveRole, "attWave"},
        {DownloadStageRole, "downloadStage"}, {TransferStageRole, "transferStage"},
        {ReplyToRole, "replyTo"}};
}

void ConversationModel::setMessages(QVector<StoredMessage> messages)
{
    beginResetModel();
    messages_ = std::move(messages);
    errorById_.clear();
    uploadProgressById_.clear();
    downloadReceivedById_.clear();
    downloadTotalById_.clear();
    downloadErrorById_.clear();
    downloadStageById_.clear();
    transferStageById_.clear();
    endResetModel();
}

int ConversationModel::appendMessage(const StoredMessage& message)
{
    // Insert keeping the list sorted by (orderKey, id). A local send and a late
    // arrival both carry orderKey ~= now, so they land at the end; a message that
    // was sent recently but arrived out of order (smaller orderKey) slots back
    // into its place among the recent tail. Scanning from the end keeps the common
    // append case O(1) (see docs-main Messages.md "Ordering and timestamps").
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

void ConversationModel::prependMessages(const QVector<StoredMessage>& messages)
{
    if (messages.isEmpty()) {
        return;
    }
    beginInsertRows({}, 0, static_cast<int>(messages.size()) - 1);
    QVector<StoredMessage> merged = messages;
    merged += messages_;
    messages_ = std::move(merged);
    endInsertRows();
}

void ConversationModel::appendMessages(const QVector<StoredMessage>& messages)
{
    if (messages.isEmpty()) {
        return;
    }
    const int row = static_cast<int>(messages_.size());
    beginInsertRows({}, row, row + static_cast<int>(messages.size()) - 1);
    messages_ += messages;
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

QVector<qint64> ConversationModel::markDeliveredThrough(qint64 uptoId)
{
    QVector<qint64> changed;
    for (int i = 0; i < messages_.size(); ++i) {
        StoredMessage& m = messages_[i];
        // Only what reached the recipient's server: a message still at our own is
        // not one a later read receipt can speak for.
        if (m.outgoing && m.id <= uptoId && m.status == DeliveryStatus::AtRecipientServer) {
            m.status = DeliveryStatus::Delivered;
            const QModelIndex idx = index(i);
            emit dataChanged(idx, idx, {StatusRole});
            changed.push_back(m.id);
        }
    }
    return changed;
}

bool ConversationModel::newestIncomingThrough(int row, qint64& outId, QString& outProtocol) const
{
    const int start = std::min(row, static_cast<int>(messages_.size()) - 1);
    for (int i = start; i >= 0; --i) {
        const StoredMessage& m = messages_[i];
        if (!m.outgoing && !m.e2eId.isEmpty() && m.type != "system") {
            outId = m.id;
            outProtocol = m.e2eId;
            return true;
        }
    }
    return false;
}

void ConversationModel::setStatusForId(qint64 id, int status)
{
    for (int i = 0; i < messages_.size(); ++i) {
        if (messages_[i].id == id) {
            messages_[i].status = status;
            const QModelIndex idx = index(i);
            emit dataChanged(idx, idx, {StatusRole});
            return;
        }
    }
}

void ConversationModel::setTextForId(qint64 id, const QString& text)
{
    for (int i = 0; i < messages_.size(); ++i) {
        if (messages_[i].id == id) {
            messages_[i].text = text;
            const QModelIndex idx = index(i);
            emit dataChanged(idx, idx, {TextRole});
            return;
        }
    }
}

void ConversationModel::setErrorForId(qint64 id, const QString& error)
{
    if (error.isEmpty()) {
        errorById_.remove(id);
    } else {
        errorById_.insert(id, error);
    }
    for (int i = 0; i < messages_.size(); ++i) {
        if (messages_[i].id == id) {
            const QModelIndex idx = index(i);
            emit dataChanged(idx, idx, {ErrorRole});
            return;
        }
    }
}

void ConversationModel::setUploadProgressForId(qint64 id, double fraction)
{
    uploadProgressById_.insert(id, fraction);
    for (int i = 0; i < messages_.size(); ++i) {
        if (messages_[i].id == id) {
            const QModelIndex idx = index(i);
            emit dataChanged(idx, idx, {UploadProgressRole});
            return;
        }
    }
}

void ConversationModel::setDownloadProgressForId(qint64 id, qint64 received, qint64 total)
{
    downloadReceivedById_.insert(id, received);
    downloadTotalById_.insert(id, total);
    downloadErrorById_.remove(id);
    for (int i = 0; i < messages_.size(); ++i) {
        if (messages_[i].id == id) {
            const QModelIndex idx = index(i);
            emit dataChanged(idx, idx,
                {DownloadingRole, DownloadReceivedRole, DownloadTotalRole, DownloadErrorRole});
            return;
        }
    }
}

void ConversationModel::setDownloadStageForId(qint64 id, int stage)
{
    downloadStageById_.insert(id, stage);
    for (int i = 0; i < messages_.size(); ++i) {
        if (messages_[i].id == id) {
            const QModelIndex idx = index(i);
            emit dataChanged(idx, idx, {DownloadStageRole});
            return;
        }
    }
}

void ConversationModel::setTransferStageForId(const qint64 id, const QString& stage)
{
    if (stage.isEmpty()) {
        transferStageById_.remove(id);
    } else {
        transferStageById_.insert(id, stage);
    }
    for (int i = 0; i < messages_.size(); ++i) {
        if (messages_[i].id == id) {
            const QModelIndex idx = index(i);
            emit dataChanged(idx, idx, {TransferStageRole});
            return;
        }
    }
}

void ConversationModel::finishDownloadForId(qint64 id, bool ok, const QString& error)
{
    downloadReceivedById_.remove(id);
    downloadTotalById_.remove(id);
    downloadStageById_.remove(id);
    transferStageById_.remove(id);
    if (ok || error.isEmpty()) {
        downloadErrorById_.remove(id);
    } else {
        downloadErrorById_.insert(id, error);
    }
    for (int i = 0; i < messages_.size(); ++i) {
        if (messages_[i].id == id) {
            const QModelIndex idx = index(i);
            emit dataChanged(idx, idx, {DownloadingRole, DownloadReceivedRole, DownloadTotalRole,
                                           DownloadErrorRole, DownloadStageRole});
            return;
        }
    }
}

void ConversationModel::setSavedPathForId(qint64 id, const QString& path)
{
    for (int i = 0; i < messages_.size(); ++i) {
        if (messages_[i].id == id) {
            messages_[i].savedPath = path;
            const QModelIndex idx = index(i);
            emit dataChanged(idx, idx, {SavedPathRole});
            return;
        }
    }
}

void ConversationModel::setPictureReadyForId(qint64 id, const bool ready)
{
    for (int i = 0; i < messages_.size(); ++i) {
        if (messages_[i].id == id) {
            messages_[i].hasPicture = ready;
            const QModelIndex idx = index(i);
            emit dataChanged(idx, idx, {PictureRole});
            return;
        }
    }
}

void ConversationModel::setBlobGoneForId(qint64 id, bool gone)
{
    for (int i = 0; i < messages_.size(); ++i) {
        if (messages_[i].id == id) {
            messages_[i].blobGone = gone;
            const QModelIndex idx = index(i);
            emit dataChanged(idx, idx, {BlobGoneRole});
            return;
        }
    }
}

void ConversationModel::editById(qint64 id, const QString& text, const QString& keyboard)
{
    for (int i = 0; i < messages_.size(); ++i) {
        if (messages_[i].id == id) {
            messages_[i].text = text;
            messages_[i].keyboard = keyboard;
            messages_[i].edited = true;
            const QModelIndex idx = index(i);
            emit dataChanged(idx, idx, {TextRole, KeyboardRole, EditedRole});
            return;
        }
    }
}

void ConversationModel::removeById(qint64 id)
{
    for (int i = 0; i < messages_.size(); ++i) {
        if (messages_[i].id == id) {
            beginRemoveRows({}, i, i);
            messages_.removeAt(i);
            endRemoveRows();
            errorById_.remove(id);
            uploadProgressById_.remove(id);
            downloadReceivedById_.remove(id);
            downloadTotalById_.remove(id);
            downloadErrorById_.remove(id);
            return;
        }
    }
}

bool ConversationModel::lastMessageOutgoing() const
{
    return !messages_.isEmpty() && messages_.last().outgoing;
}

// ---------------- OpenAccountsModel ----------------

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
    case I2pFacadeRole: return a.i2pFacade;
    default: return {};
    }
}

QHash<int, QByteArray> OpenAccountsModel::roleNames() const
{
    return {{IdRole, "accountId"}, {NameRole, "name"}, {FingerprintRole, "fingerprint"},
        {OpenRole, "open"}, {ActiveRole, "active"}, {OnlineRole, "online"},
        {ConnectedRole, "connected"}, {EncryptedRole, "encrypted"}, {UnreadRole, "unread"},
        {ActiveFacadeRole, "activeFacade"}, {I2pFacadeRole, "i2pFacade"}};
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
        // Newest at the top, matching how the panel reads top-to-bottom.
        beginInsertRows({}, 0, 0);
        ops_.prepend(row);
        endInsertRows();
        return;
    }
    ops_[i] = row;
    const QModelIndex idx = index(i);
    emit dataChanged(idx, idx);
}

void OperationListModel::update(const QString& id, const QString& status, const QString& detail,
    double progress, int state)
{
    const int i = indexOf(id);
    if (i < 0) {
        return;
    }
    OperationRow& o = ops_[i];
    o.status = status;
    o.detail = detail;
    if (progress >= 0.0) {
        o.progress = progress;
    }
    o.state = state;
    const QModelIndex idx = index(i);
    emit dataChanged(idx, idx, {StatusRole, DetailRole, ProgressRole, StateRole});
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
