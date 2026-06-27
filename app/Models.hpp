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
    QString fingerprint;   // contact fingerprint, or a group id when isGroup
    QString name;
    QString lastText;
    qint64 lastTime = 0;
    int unread = 0;
    bool isGroup = false;
};

class ContactListModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Roles {
        FingerprintRole = Qt::UserRole + 1, NameRole, LastTextRole, LastTimeRole, UnreadRole,
        IsGroupRole
    };
    using QAbstractListModel::QAbstractListModel;

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setContacts(QVector<ContactRow> contacts);
    // Bumps a contact's preview/unread, inserting it if new, and keeps the
    // list sorted by most-recent.
    void touch(const QString& fingerprint, const QString& name, const QString& lastText,
        qint64 lastTime, bool incrementUnread, bool isGroup = false);
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
        SenderRole, TimeRole, StatusRole, MsgIdRole, ErrorRole, UploadProgressRole, DayRole,
        DownloadingRole, DownloadReceivedRole, DownloadTotalRole, DownloadErrorRole, SavedPathRole,
        BlobGoneRole, DownloadStageRole, ReplyToRole
    };
    using QAbstractListModel::QAbstractListModel;

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setMessages(QVector<StoredMessage> messages);
    int appendMessage(const StoredMessage& message);  // returns row
    // Inserts a batch of older messages at the front (paging up into history).
    void prependMessages(const QVector<StoredMessage>& messages);
    // Appends a batch of newer messages at the end (paging down toward the newest).
    void appendMessages(const QVector<StoredMessage>& messages);
    // The row index of the message with this id, or -1 (for scroll-to-message).
    Q_INVOKABLE int rowForId(qint64 id) const;
    // Marks loaded outgoing messages with id <= uptoId currently at AtSenderServer
    // or AtRecipientServer as Delivered (read high-water); returns the changed ids.
    QVector<qint64> markDeliveredThrough(qint64 uptoId);
    // The id + protocol id of the newest incoming message at or before `row`
    // (for the read high-water). Returns false when there is none.
    bool newestIncomingThrough(int row, qint64& outId, QString& outProtocol) const;
    void setStatusForId(qint64 id, int status);
    // Attaches (or, when empty, clears) a delivery-error string for a message,
    // shown inline on a failed outgoing bubble. Session-only; not persisted.
    void setErrorForId(qint64 id, const QString& error);
    // Sets the upload progress fraction (0..1) for an outgoing file in flight;
    // a negative value (the default) means "no determinate progress". Session-only.
    void setUploadProgressForId(qint64 id, double fraction);
    // Download progress for an incoming attachment being saved: received/total
    // ciphertext bytes (total > 0 means a download is in flight). Session-only.
    void setDownloadProgressForId(qint64 id, qint64 received, qint64 total);
    // The fetch stage of an in-flight download (a BlobFetchStage: 0 connecting,
    // 1 downloading, 2 reconnecting), so a stalled transfer reads as "reconnecting"
    // rather than a frozen bar. Session-only.
    void setDownloadStageForId(qint64 id, int stage);
    // Marks a download finished: ok clears the progress; otherwise records an
    // inline error and clears the progress. Session-only.
    void finishDownloadForId(qint64 id, bool ok, const QString& error);
    // Records where an incoming attachment was saved, so the bubble can offer to
    // open it instead of re-saving.
    void setSavedPathForId(qint64 id, const QString& path);
    // Marks an incoming attachment as gone from the store (404/410): the bubble
    // shows "Not found" and drops the Save button.
    void setBlobGoneForId(qint64 id, bool gone);
    // Replaces a message's text and keyboard in place and marks it edited.
    void editById(qint64 id, const QString& text, const QString& keyboard);
    // Removes a message from the open window (delete with no trace). No-op if the
    // id is not currently loaded.
    void removeById(qint64 id);
    // True when the most recent message is one the local user sent. The view
    // uses this to always scroll an own message into view, while following an
    // incoming message only when the view was already pinned to the bottom.
    Q_INVOKABLE bool lastMessageOutgoing() const;

private:
    QVector<StoredMessage> messages_;
    QHash<qint64, QString> errorById_;
    QHash<qint64, double> uploadProgressById_;
    QHash<qint64, qint64> downloadReceivedById_;
    QHash<qint64, qint64> downloadTotalById_;
    QHash<qint64, QString> downloadErrorById_;
    QHash<qint64, int> downloadStageById_;
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
    // The facade the account is connected through, and whether it is an I2P
    // facade (host ends in ".b32.i2p"). i2pFacade drives the positive green
    // marking in the account list; a clearnet facade reads grey.
    QString activeFacade;
    bool i2pFacade = false;
};

class OpenAccountsModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Roles {
        IdRole = Qt::UserRole + 1, NameRole, FingerprintRole, OpenRole, ActiveRole,
        OnlineRole, ConnectedRole, EncryptedRole, UnreadRole, ActiveFacadeRole, I2pFacadeRole
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
