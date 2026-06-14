// Bazarish project (c) 2026
#pragma once

#include "Models.hpp"
#include "TranscriptStore.hpp"

#include <QObject>
#include <QString>
#include <QThread>
#include <QVariantMap>

#include <memory>

class QTimer;

namespace bazarish::client {
class Session;
}

namespace bazarish::app {

// Runs all blocking Session work (open, subscribe, send with retries, sync) on
// a dedicated thread so the UI never freezes. Lives on that worker thread;
// commands arrive via queued calls and results leave via queued signals.
class SessionWorker : public QObject {
    Q_OBJECT
public:
    ~SessionWorker() override;

public slots:
    void openProfile(const QString& dir, const QString& passphrase);
    void connectAndSubscribe(const QString& host, int port, const QString& basePath,
        const QString& serverFp, int days);
    void sync();
    void sendText(const QString& peer, const QString& text, qint64 localId,
        const QString& protocolId);
    void sendFile(const QString& peer, const QString& localPath, qint64 localId,
        const QString& protocolId);
    void sendReceipt(const QString& peer, const QString& refId);
    void sendCallback(const QString& peer, const QString& data, const QString& ref);
    void sendCommand(const QString& peer, const QString& command, const QString& args);
    void sendEdit(const QString& peer, const QString& refId, const QString& text);
    void addByInvite(const QString& uri, const QString& intro);
    void addByUsername(const QString& alias, const QString& intro);
    void addByFingerprint(const QString& fingerprint, const QString& intro);
    void registerAlias(const QString& alias);
    void requestInvite();
    void saveAttachment(const QString& ref, const QString& key, const QString& destPath);
    void exportProfile(const QString& path, const QString& password);

signals:
    void opened(const QString& fingerprint, const QString& displayName, bool connected,
        const QString& subscriptionText);
    void openFailed(const QString& error);
    void connectionChanged(bool connected, const QString& subscriptionText);
    void messageReceived(const QVariantMap& message);
    void contactsRefreshed(const QStringList& fingerprints);
    void sendProgress(qint64 localId, int state);  // 1 = accepted by own server (grey)
    void sendResult(qint64 localId, bool ok, const QString& error);
    void actionOk(const QString& info);
    void actionFailed(const QString& error);
    void inviteReady(const QString& uri);

private:
    void ensureSyncTimer();
    std::unique_ptr<bazarish::client::Session> session_;
    QTimer* syncTimer_ = nullptr;
};

// QML-facing facade: owns the worker thread, the models and the transcript
// store; exposes invokable commands and observable properties.
class SessionController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString fingerprint READ fingerprint NOTIFY identityChanged)
    Q_PROPERTY(QString displayName READ displayName NOTIFY identityChanged)
    Q_PROPERTY(bool connected READ connected NOTIFY connectedChanged)
    Q_PROPERTY(QString subscriptionText READ subscriptionText NOTIFY connectedChanged)
    Q_PROPERTY(QString activePeer READ activePeer NOTIFY activePeerChanged)
    Q_PROPERTY(QObject* contacts READ contacts CONSTANT)
    Q_PROPERTY(QObject* conversation READ conversation CONSTANT)
    Q_PROPERTY(bool sendReceipts READ sendReceipts WRITE setSendReceipts NOTIFY sendReceiptsChanged)
    // True while the composer is editing a previously sent message; editingText
    // is its current text, so the composer can prefill the field.
    Q_PROPERTY(bool editing READ editing NOTIFY editingChanged)
    Q_PROPERTY(QString editingText READ editingText NOTIFY editingChanged)
public:
    explicit SessionController(QObject* parent = nullptr);
    ~SessionController() override;

    QString fingerprint() const { return fingerprint_; }
    QString displayName() const { return displayName_; }
    bool connected() const { return connected_; }
    QString subscriptionText() const { return subscriptionText_; }
    QString activePeer() const { return activePeer_; }
    QObject* contacts() { return &contacts_; }
    QObject* conversation() { return &conversation_; }
    bool sendReceipts() const { return sendReceipts_; }
    void setSendReceipts(bool on) { if (sendReceipts_ != on) { sendReceipts_ = on; emit sendReceiptsChanged(); } }
    bool editing() const { return editing_; }
    QString editingText() const { return editingText_; }

    // Opens a profile on the worker thread (dir + id + passphrase).
    void open(const QString& dir, const QString& profileId, const QString& passphrase);

    Q_INVOKABLE void connectServer(
        const QString& host, int port, const QString& basePath, const QString& serverFp);
    Q_INVOKABLE void openConversation(const QString& peer);
    Q_INVOKABLE void sendText(const QString& text);
    Q_INVOKABLE void sendFile(const QString& fileUrl);
    // Inline-keyboard button presses in the active conversation: a callback
    // (button data + the keyboard message's protocol id) or a command button.
    Q_INVOKABLE void sendCallback(const QString& data, const QString& refMsgId);
    Q_INVOKABLE void sendCommand(const QString& command, const QString& args);
    // Editing one's own message: start (prefilling the composer), commit the new
    // text (updates our copy and sends an edit to the peer), or cancel.
    Q_INVOKABLE void beginEdit(qint64 localId, const QString& protocolId, const QString& text);
    Q_INVOKABLE void commitEdit(const QString& newText);
    Q_INVOKABLE void cancelEdit();
    Q_INVOKABLE void addByInvite(const QString& uri, const QString& intro);
    Q_INVOKABLE void addByUsername(const QString& alias, const QString& intro);
    Q_INVOKABLE void addByFingerprint(const QString& fingerprint, const QString& intro);
    Q_INVOKABLE void registerAlias(const QString& alias);
    Q_INVOKABLE void requestInvite();
    Q_INVOKABLE void saveAttachment(const QString& ref, const QString& key, const QString& fileUrl);
    Q_INVOKABLE void exportProfile(const QString& fileUrl, const QString& password);
    Q_INVOKABLE QString shortFingerprint(const QString& fp) const;

signals:
    void identityChanged();
    void connectedChanged();
    void activePeerChanged();
    void sendReceiptsChanged();
    void editingChanged();
    void openFailed(const QString& error);
    void actionOk(const QString& info);
    void actionFailed(const QString& error);
    void inviteReady(const QString& uri);

signals:  // to worker
    void requestConnect(const QString& host, int port, const QString& basePath,
        const QString& serverFp, int days);
    void requestSendText(const QString& peer, const QString& text, qint64 localId,
        const QString& protocolId);
    void requestSendFile(const QString& peer, const QString& localPath, qint64 localId,
        const QString& protocolId);
    void requestSendReceipt(const QString& peer, const QString& refId);
    void requestSendCallback(const QString& peer, const QString& data, const QString& ref);
    void requestSendCommand(const QString& peer, const QString& command, const QString& args);
    void requestSendEdit(const QString& peer, const QString& refId, const QString& text);
    void requestAddByInvite(const QString& uri, const QString& intro);
    void requestAddByUsername(const QString& alias, const QString& intro);
    void requestAddByFingerprint(const QString& fingerprint, const QString& intro);
    void requestRegisterAlias(const QString& alias);
    void requestInviteSig();
    void requestSaveAttachment(const QString& ref, const QString& key, const QString& destPath);
    void requestExport(const QString& path, const QString& password);
    void requestOpen(const QString& dir, const QString& passphrase);

private slots:
    void onOpened(const QString& fingerprint, const QString& displayName, bool connected,
        const QString& subscriptionText);
    void onConnectionChanged(bool connected, const QString& subscriptionText);
    void onMessageReceived(const QVariantMap& message);
    void onSendProgress(qint64 localId, int state);
    void onSendResult(qint64 localId, bool ok, const QString& error);

private:
    QThread thread_;
    SessionWorker* worker_ = nullptr;
    TranscriptStore store_;
    ContactListModel contacts_;
    ConversationModel conversation_;

    QString profileId_;
    QString fingerprint_;
    QString displayName_;
    bool connected_ = false;
    QString subscriptionText_;
    QString activePeer_;
    bool sendReceipts_ = true;
    // Edit-in-progress state for the composer (0 / empty when not editing).
    bool editing_ = false;
    qint64 editingLocalId_ = 0;
    QString editingProtocolId_;
    QString editingText_;
    // Current delivery status per outgoing local id, so a later/lower signal
    // (e.g. "yellow" arriving after "green") never downgrades the tick.
    QHash<qint64, int> statusById_;
    void bumpStatus(qint64 localId, int status);
};

}  // namespace bazarish::app
