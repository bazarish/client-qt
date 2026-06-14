// Bazarish project (c) 2026
#include "Models.hpp"

#include <algorithm>

namespace bazarish::app {

// ---------------- ProfileListModel ----------------

int ProfileListModel::rowCount(const QModelIndex&) const
{
    return static_cast<int>(profiles_.size());
}

QVariant ProfileListModel::data(const QModelIndex& index, int role) const
{
    if (index.row() < 0 || index.row() >= profiles_.size()) {
        return {};
    }
    const ProfileRow& p = profiles_[index.row()];
    switch (role) {
    case IdRole: return p.id;
    case NameRole: return p.name;
    case FingerprintRole: return p.fingerprint;
    case EncryptedRole: return p.encrypted;
    case ConnectedRole: return p.connected;
    default: return {};
    }
}

QHash<int, QByteArray> ProfileListModel::roleNames() const
{
    return {{IdRole, "profileId"}, {NameRole, "name"}, {FingerprintRole, "fingerprint"},
        {EncryptedRole, "encrypted"}, {ConnectedRole, "connected"}};
}

void ProfileListModel::setProfiles(QVector<ProfileRow> profiles)
{
    beginResetModel();
    profiles_ = std::move(profiles);
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
    default: return {};
    }
}

QHash<int, QByteArray> ContactListModel::roleNames() const
{
    return {{FingerprintRole, "fingerprint"}, {NameRole, "name"}, {LastTextRole, "lastText"},
        {LastTimeRole, "lastTime"}, {UnreadRole, "unread"}};
}

void ContactListModel::setContacts(QVector<ContactRow> contacts)
{
    beginResetModel();
    contacts_ = std::move(contacts);
    std::stable_sort(contacts_.begin(), contacts_.end(),
        [](const ContactRow& a, const ContactRow& b) { return a.lastTime > b.lastTime; });
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
        [](const ContactRow& a, const ContactRow& b) { return a.lastTime > b.lastTime; });
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

void ContactListModel::clearUnread(const QString& fingerprint)
{
    const int i = indexOf(fingerprint);
    if (i >= 0 && contacts_[i].unread != 0) {
        contacts_[i].unread = 0;
        const QModelIndex idx = index(i);
        emit dataChanged(idx, idx, {UnreadRole});
    }
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
    case ProtocolIdRole: return m.protocolId;
    case EditedRole: return m.edited;
    case TimeRole: return m.ts;
    case StatusRole: return m.status;
    case MsgIdRole: return m.id;
    default: return {};
    }
}

QHash<int, QByteArray> ConversationModel::roleNames() const
{
    return {{OutgoingRole, "outgoing"}, {TypeRole, "type"}, {TextRole, "text"},
        {AttNameRole, "attName"}, {AttMimeRole, "attMime"}, {AttSizeRole, "attSize"},
        {AttRefRole, "attRef"}, {AttKeyRole, "attKey"}, {KeyboardRole, "keyboard"},
        {ProtocolIdRole, "protocolId"}, {EditedRole, "edited"}, {TimeRole, "time"},
        {StatusRole, "status"}, {MsgIdRole, "msgId"}};
}

void ConversationModel::setMessages(QVector<StoredMessage> messages)
{
    beginResetModel();
    messages_ = std::move(messages);
    endResetModel();
}

int ConversationModel::appendMessage(const StoredMessage& message)
{
    const int row = static_cast<int>(messages_.size());
    beginInsertRows({}, row, row);
    messages_.push_back(message);
    endInsertRows();
    return row;
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

}  // namespace bazarish::app
