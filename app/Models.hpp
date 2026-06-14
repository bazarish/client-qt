// Bazarish project (c) 2026
#pragma once

#include "TranscriptStore.hpp"

#include <QAbstractListModel>
#include <QHash>
#include <QString>
#include <QVector>

namespace bazarish::app {

// Profiles shown in the picker (public metadata only).
struct ProfileRow {
    QString id;
    QString name;
    QString fingerprint;
    bool encrypted = false;
    bool connected = false;
};

class ProfileListModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Roles { IdRole = Qt::UserRole + 1, NameRole, FingerprintRole, EncryptedRole, ConnectedRole };
    using QAbstractListModel::QAbstractListModel;

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setProfiles(QVector<ProfileRow> profiles);

private:
    QVector<ProfileRow> profiles_;
};

// The chat list: one row per contact, with a last-message preview.
struct ContactRow {
    QString fingerprint;
    QString name;
    QString lastText;
    qint64 lastTime = 0;
    int unread = 0;
};

class ContactListModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Roles {
        FingerprintRole = Qt::UserRole + 1, NameRole, LastTextRole, LastTimeRole, UnreadRole
    };
    using QAbstractListModel::QAbstractListModel;

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setContacts(QVector<ContactRow> contacts);
    // Bumps a contact's preview/unread, inserting it if new, and keeps the
    // list sorted by most-recent.
    void touch(const QString& fingerprint, const QString& name, const QString& lastText,
        qint64 lastTime, bool incrementUnread);
    void clearUnread(const QString& fingerprint);
    // Sum of unread counts across all contacts (the account's unread total).
    int totalUnread() const;

private:
    int indexOf(const QString& fingerprint) const;
    void resort();
    QVector<ContactRow> contacts_;
};

// The open conversation's messages.
class ConversationModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Roles {
        OutgoingRole = Qt::UserRole + 1, TypeRole, TextRole, AttNameRole, AttMimeRole,
        AttSizeRole, AttRefRole, AttKeyRole, KeyboardRole, ProtocolIdRole, EditedRole,
        TimeRole, StatusRole, MsgIdRole
    };
    using QAbstractListModel::QAbstractListModel;

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setMessages(QVector<StoredMessage> messages);
    int appendMessage(const StoredMessage& message);  // returns row
    void setStatusForId(qint64 id, int status);
    // Replaces a message's text and keyboard in place and marks it edited.
    void editById(qint64 id, const QString& text, const QString& keyboard);

private:
    QVector<StoredMessage> messages_;
};

// One account in the unified account list. Covers every on-disk profile, with
// live status merged in for the ones that are currently open.
struct AccountRow {
    QString id;
    QString name;
    QString fingerprint;
    bool open = false;       // a session is loaded for it
    bool active = false;     // the active UI account
    bool online = false;     // loaded and syncing (receiving)
    bool connected = false;  // the last sync reached the facade
    bool encrypted = false;  // needs a passphrase to open
    int unread = 0;
};

class OpenAccountsModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Roles {
        IdRole = Qt::UserRole + 1, NameRole, FingerprintRole, OpenRole, ActiveRole,
        OnlineRole, ConnectedRole, EncryptedRole, UnreadRole
    };
    using QAbstractListModel::QAbstractListModel;

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setAccounts(QVector<AccountRow> accounts);

private:
    QVector<AccountRow> accounts_;
};

}  // namespace bazarish::app
