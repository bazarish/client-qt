// Bazarish project (c) 2026
#pragma once

#include "AccountEvents.hpp"
#include "AppEvents.hpp"
#include "TranscriptStore.hpp"

#include <QAbstractListModel>
#include <QHash>
#include <QString>
#include <QVector>

namespace bazarish::app {

class AccountListModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Roles {
        IdRole = Qt::UserRole + 1, NameRole, FingerprintRole, EncryptedRole, OpenRole, PrimaryRole,
        OtherPrimariesRole, ServerlessRole, RekeyingRole
    };
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
    int mentions = 0;
};

class ContactListModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Roles {
        FingerprintRole = Qt::UserRole + 1, NameRole, LastTextRole, LastTimeRole, UnreadRole,
        PinnedRole, SavedRole, MentionsRole
    };
    using QAbstractListModel::QAbstractListModel;

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setContacts(QVector<ContactRow> contacts);
    void touch(const QString& fingerprint, const QString& name, const QString& lastText,
        qint64 lastTime);
    void setUnread(const QString& fingerprint, int count, int mentions);
    int totalUnread(const QString& except) const;
    void remove(const QString& fingerprint);

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

struct ViewLanding {
    enum Mode { eEnd, eRow, eUnread, eMessage, eQuote };
    Mode mode = eEnd;
    // A row for eRow, a message id for the others.
    qint64 at = 0;
};

struct RowReactions {
    QVariantList summary;
    QStringList fresh;

    friend bool operator==(const RowReactions&, const RowReactions&) = default;
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
        TransferStageRole, ReplyToRole, PictureRole, CarriedRole,
        DurationRole, WaveRole, AuthorRole, ContactableRole, FromHostRole, DeviceRole,
        ReactionsRole, FreshReactionsRole, PictureVersionRole, MentionsMeRole
    };
    using QAbstractListModel::QAbstractListModel;

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setMessages(QVector<StoredMessage> messages, ViewLanding landing = {});
    void setLanding(ViewLanding landing);
    Q_INVOKABLE QVariantMap landing() const;
    // Rows not loaded yet keep theirs until they come; a reset drops those of rows it left out.
    void putReactions(const QString& e2eId, const RowReactions& reactions);
    void notePictureChanged(const QString& e2eId);
    int rowForE2e(const QString& e2eId) const;
    QStringList loadedE2eIds() const;
    void retranslate();
    int appendMessage(const StoredMessage& message);
    void prependMessages(const QVector<StoredMessage>& messages);
    void appendMessages(const QVector<StoredMessage>& messages);
    Q_INVOKABLE int rowForId(qint64 id) const;
    Q_INVOKABLE QVariantMap lastOwnText() const;
    void markDeliveredThrough(qint64 uptoId);
    bool newestIncomingThrough(int row, qint64& outId, QString& outProtocol, qint64& outSentAt) const;
    void setErrorForId(qint64 id, const QString& error);
    void setUploadProgressForId(qint64 id, double fraction);
    void setDownloadProgressForId(qint64 id, qint64 received, qint64 total);
    void setTransferStageForId(qint64 id, const QString& stage);
    void finishDownloadForId(qint64 id, bool ok, const QString& error);
    void markPictureMissingForId(qint64 id);
    void replaceById(const StoredMessage& message);
    void removeById(qint64 id);
    Q_INVOKABLE bool lastMessageOutgoing() const;

private:
    void notifyRow(int row, const QList<int>& roles);
    QVector<StoredMessage> unknownOf(const QVector<StoredMessage>& messages) const;

    QVector<StoredMessage> messages_;
    QHash<qint64, LiveMessageState> live_;
    QHash<QString, RowReactions> reactions_;
    QHash<QString, int> pictureVersions_;
    ViewLanding landing_;
};

class OpenAccountsModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Roles {
        IdRole = Qt::UserRole + 1, NameRole, FingerprintRole, OpenRole, ActiveRole,
        OnlineRole, ConnectedRole, EncryptedRole, UnreadRole, ActiveFacadeRole, PrimaryRole,
        OtherPrimariesRole, ServerlessRole
    };
    using QAbstractListModel::QAbstractListModel;

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setAccounts(QVector<AccountRow> accounts);

private:
    QVector<AccountRow> accounts_;
};

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
    void remove(const QString& id);
    int runningCount() const;

private:
    int indexOf(const QString& id) const;
    QVector<OperationRow> ops_;
};

}  // namespace bazarish::app
