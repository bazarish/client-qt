// Bazarish project (c) 2026
#pragma once

#include "TranscriptStore.hpp"

#include <QAbstractListModel>
#include <QHash>
#include <QString>
#include <QVector>

namespace bazarish::app {

// Accounts shown in the picker (public metadata only).
struct AccountListRow {
    QString id;
    QString name;
    QString fingerprint;
    bool encrypted = false;
    // A session is loaded for it. An encrypted account that is open has already
    // been unlocked, and asking for its passphrase again - or drawing it with a
    // closed lock - says the opposite of what is true.
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

// The chat list: one row per contact, with a last-message preview.
struct ContactRow {
    QString fingerprint;   // contact fingerprint
    QString name;
    QString lastText;
    qint64 lastTime = 0;
    int unread = 0;
    bool pinned = false;   // kept at the top of the list, before the recent sort
    // The saved-messages chat: always in the list and never deleted, but ordered
    // and pinned like any other. What this drives is the mark it carries.
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
    // Bumps a contact's preview/unread, inserting it if new, and keeps the
    // list sorted by most-recent.
    void touch(const QString& fingerprint, const QString& name, const QString& lastText,
        qint64 lastTime, bool incrementUnread);
    // Sets a contact's unread badge to an exact count (from the persistent store),
    // inserting nothing - a no-op for a row not present. Used to keep the badge in
    // sync with the read high-water rather than a fragile running increment.
    void setUnread(const QString& fingerprint, int count);
    // Sum of unread counts across all contacts (the account's unread total).
    int totalUnread() const;
    // Drops one row, for a contact that has been removed here or on another
    // device. A no-op for a row that is not there.
    void remove(const QString& fingerprint);
    // Whether this account already holds them. The chat list is the same book the
    // add path has to consult, so asking it here beats keeping a second copy.
    bool has(const QString& fingerprint) const { return indexOf(fingerprint) >= 0; }

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
        AttSizeRole, AttRefRole, KeyboardRole, E2eIdRole, EditedRole,
        ForwardedRole,
        TimeRole, StatusRole, MsgIdRole, ErrorRole, UploadProgressRole, DayRole,
        DownloadingRole, DownloadReceivedRole, DownloadTotalRole, DownloadErrorRole, SavedPathRole,
        BlobGoneRole, DownloadStageRole, TransferStageRole, ReplyToRole, PictureRole,
        DurationRole, WaveRole
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
    // Marks loaded outgoing messages with id <= uptoId currently at
    // AtRecipientServer as Delivered (read high-water); returns the changed ids.
    QVector<qint64> markDeliveredThrough(qint64 uptoId);
    // The id + protocol id of the newest incoming message at or before `row`
    // (for the read high-water). Returns false when there is none.
    bool newestIncomingThrough(int row, qint64& outId, QString& outProtocol, qint64& outSentAt) const;
    void setStatusForId(qint64 id, int status);
    // Replaces a row's text in place (a system note tracking a running operation).
    void setTextForId(qint64 id, const QString& text);
    void setTypeForId(qint64 id, const QString& type);
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
    // What a direct transfer is doing right now, in the user's words (empty
    // clears it). Most of a transfer happens before the first byte, so the bubble
    // says which step it is on instead of one long "connecting". Session-only.
    void setTransferStageForId(qint64 id, const QString& stage);
    // Marks a download finished: ok clears the progress; otherwise records an
    // inline error and clears the progress. Session-only.
    void finishDownloadForId(qint64 id, bool ok, const QString& error);
    // Records where an incoming attachment was saved, so the bubble can offer to
    // open it instead of re-saving.
    void setSavedPathForId(qint64 id, const QString& path);
    // Marks a message whose picture this account now holds, so the bubble draws
    // it. What it draws is served out of the account database, not off disk.
    void setPictureReadyForId(qint64 id, bool ready);
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
    QHash<qint64, QString> transferStageById_;
};

// One account in the unified account list. Covers every on-disk account, with
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
    // The facade the account is connected through. Always an I2P one - the
    // client speaks to a server over I2P and nothing else - so it is shown as
    // an address, not as a kind of connection.
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

// A long-running background operation surfaced in the activity panel - a contact
// add, a message or file send, a download, or a call - with a human-readable
// status that updates as it progresses. Session-only; never persisted.
struct OperationRow {
    QString id;       // stable correlation key, e.g. "send:42" / "contact:<fp>" / "call:<id>"
    QString kind;     // "contact" | "send" | "file-up" | "file-down" | "call"
    QString title;    // primary line, e.g. "Adding Alice" / "photo.jpg"
    QString status;   // human-readable current status
    QString detail;   // optional secondary line (server phase, elapsed, error)
    double progress = -1.0;  // 0..1 for a determinate bar (files); < 0 = indeterminate
    int state = 0;    // 0 running, 1 done, 2 failed
    qint64 startedAt = 0;
    QString peer;     // associated contact fingerprint, for tap-through (optional)
    // The transfer this row can stop, by the file's protocol id. Empty when the
    // operation cannot be interrupted.
    QString cancelId;
};

// kOperationState* mirror OperationRow::state for readable call sites.
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

    // Insert a new operation at the top, or update the existing one with this id.
    void upsert(const OperationRow& row);
    // Update an existing operation's mutable fields (no-op when the id is absent).
    // A negative progress leaves the current progress unchanged.
    void update(const QString& id, const QString& status, const QString& detail, double progress,
        int state);
    // Names the transfer an existing row can stop. A file send opens its row when
    // the announcement goes out, long before the recipient asks for the bytes;
    // the stop button belongs to it only once there is a transfer to stop.
    void setCancelId(const QString& id, const QString& cancelId);
    void remove(const QString& id);
    // Operations still running (drives the floating button's visibility/count).
    int runningCount() const;
    // The row of an operation by id, or -1.
    int indexOf(const QString& id) const;

private:
    QVector<OperationRow> ops_;
};

}  // namespace bazarish::app
