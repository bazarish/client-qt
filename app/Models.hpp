// Bazarish project (c) 2026
#pragma once

#include "TranscriptStore.hpp"

#include <QAbstractListModel>
#include <QHash>
#include <QString>
#include <QVector>

namespace bazarish::app {

struct AccountListRow {
    QString id;
    QString name;
    QString fingerprint;
    bool encrypted = false;
    bool open = false;
};

class AccountListModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Roles { IdRole = Qt::UserRole + 1, NameRole, FingerprintRole, EncryptedRole, OpenRole };
    using QAbstractListModel::QAbstractListModel;

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setAccounts(QVector<AccountListRow> accounts);

private:
    QVector<AccountListRow> accounts_;
};

struct ContactRow {
    QString fingerprint;
    QString name;
    QString lastText;
    qint64 lastTime = 0;
    int unread = 0;
    bool pinned = false;
    bool saved = false;
};

class ContactListModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Roles {
        FingerprintRole = Qt::UserRole + 1, NameRole, LastTextRole, LastTimeRole, UnreadRole,
        PinnedRole, SavedRole
    };
    using QAbstractListModel::QAbstractListModel;

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setContacts(QVector<ContactRow> contacts);
    void touch(const QString& fingerprint, const QString& name, const QString& lastText,
        qint64 lastTime, bool incrementUnread);
    void setUnread(const QString& fingerprint, int count);
    int totalUnread() const;
    void remove(const QString& fingerprint);
    bool has(const QString& fingerprint) const { return indexOf(fingerprint) >= 0; }

private:
    int indexOf(const QString& fingerprint) const;
    void resort();
    QVector<ContactRow> contacts_;
};

struct LiveMessageState {
    QString error;
    QString downloadError;
    QString transferStage;
    double uploadProgress = -1.0;
    qint64 downloadReceived = 0;
    qint64 downloadTotal = 0;
    bool downloading = false;
};

class ConversationModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Roles {
        OutgoingRole = Qt::UserRole + 1, TypeRole, TextRole, AttNameRole, AttMimeRole,
        AttSizeRole, AttRefRole, KeyboardRole, E2eIdRole, EditedRole,
        ForwardedRole,
        TimeRole, StatusRole, MsgIdRole, ErrorRole, UploadProgressRole, DayRole,
        DownloadingRole, DownloadReceivedRole, DownloadTotalRole, DownloadErrorRole, SavedPathRole,
        TransferStageRole, ReplyToRole, PictureRole,
        DurationRole, WaveRole
    };
    using QAbstractListModel::QAbstractListModel;

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setMessages(QVector<StoredMessage> messages);
    void retranslate();
    int appendMessage(const StoredMessage& message);
    void prependMessages(const QVector<StoredMessage>& messages);
    void appendMessages(const QVector<StoredMessage>& messages);
    Q_INVOKABLE int rowForId(qint64 id) const;
    QVector<qint64> markDeliveredThrough(qint64 uptoId);
    bool newestIncomingThrough(int row, qint64& outId, QString& outProtocol, qint64& outSentAt) const;
    void setStatusForId(qint64 id, int status);
    void setTextForId(qint64 id, const QString& text);
    void setTypeForId(qint64 id, const QString& type);

    void setErrorForId(qint64 id, const QString& error);
    void setUploadProgressForId(qint64 id, double fraction);
    void setDownloadProgressForId(qint64 id, qint64 received, qint64 total);
    void setTransferStageForId(qint64 id, const QString& stage);
    void finishDownloadForId(qint64 id, bool ok, const QString& error);
    void setSavedPathForId(qint64 id, const QString& path);
    void setPictureReadyForId(qint64 id, bool ready);
    void editById(qint64 id, const QString& text, const QString& keyboard);
    void removeById(qint64 id);
    Q_INVOKABLE bool lastMessageOutgoing() const;

private:
    void notifyRow(int row, const QList<int>& roles);

    QVector<StoredMessage> messages_;
    QHash<qint64, LiveMessageState> live_;
};

struct AccountRow {
    QString id;
    QString name;
    QString fingerprint;
    bool open = false;
    bool active = false;
    bool online = false;
    bool connected = false;
    bool encrypted = false;
    int unread = 0;
    QString activeFacade;
};

class OpenAccountsModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Roles {
        IdRole = Qt::UserRole + 1, NameRole, FingerprintRole, OpenRole, ActiveRole,
        OnlineRole, ConnectedRole, EncryptedRole, UnreadRole, ActiveFacadeRole
    };
    using QAbstractListModel::QAbstractListModel;

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setAccounts(QVector<AccountRow> accounts);

private:
    QVector<AccountRow> accounts_;
};

struct OperationRow {
    QString id;
    QString kind;
    QString title;
    QString status;
    QString detail;
    double progress = -1.0;
    int state = 0;
    qint64 startedAt = 0;
    QString peer;
    QString cancelId;
};

enum OperationState { eOpRunning = 0, eOpDone = 1, eOpFailed = 2 };

class OperationListModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Roles {
        OpIdRole = Qt::UserRole + 1, KindRole, TitleRole, StatusRole, DetailRole, ProgressRole,
        StateRole, StartedAtRole, PeerRole, CancelIdRole
    };
    using QAbstractListModel::QAbstractListModel;

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void upsert(const OperationRow& row);
    void update(const QString& id, const QString& status, const QString& detail, double progress,
        int state);
    void setCancelId(const QString& id, const QString& cancelId);
    void remove(const QString& id);
    int runningCount() const;
    int indexOf(const QString& id) const;

private:
    QVector<OperationRow> ops_;
};

}  // namespace bazarish::app
