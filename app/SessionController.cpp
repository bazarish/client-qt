// Bazarish project (c) 2026
#include "SessionController.hpp"

#include "DeliveryStatus.hpp"
#include "Session.hpp"

#include <QRandomGenerator>
#include <QTimer>
#include <QUrl>

#include <ctime>
#include <exception>

namespace bazarish::app {

using bazarish::client::IncomingMessage;
using bazarish::client::ServerEndpoint;
using bazarish::client::Session;

namespace {
qint64 nowSeconds()
{
    return static_cast<qint64>(std::time(nullptr));
}
}  // namespace

// ============================ SessionWorker ============================

SessionWorker::~SessionWorker() = default;

void SessionWorker::ensureSyncTimer()
{
    if (syncTimer_ == nullptr) {
        syncTimer_ = new QTimer(this);
        syncTimer_->setInterval(3000);
        connect(syncTimer_, &QTimer::timeout, this, &SessionWorker::sync);
    }
    if (!syncTimer_->isActive()) {
        syncTimer_->start();
    }
}

void SessionWorker::openProfile(const QString& dir, const QString& passphrase)
{
    try {
        session_ = std::make_unique<Session>(
            Session::open(dir.toStdString(), passphrase.toStdString()));
    } catch (const std::exception& e) {
        emit openFailed(QString::fromUtf8(e.what()));
        return;
    }
    const bool connected = session_->isConnected();
    emit opened(QString::fromStdString(session_->fingerprint()),
        QString::fromStdString(session_->displayName()), connected,
        connected ? "connected" : "");
    QStringList fps;
    for (const std::string& fp : session_->contactFingerprints()) {
        fps << QString::fromStdString(fp);
    }
    emit contactsRefreshed(fps);
    if (connected) {
        ensureSyncTimer();
        sync();
    }
}

void SessionWorker::connectAndSubscribe(const QString& host, int port, const QString& basePath,
    const QString& serverFp, int days)
{
    if (!session_) {
        return;
    }
    try {
        ServerEndpoint endpoint;
        endpoint.host = host.toStdString();
        endpoint.port = port;
        endpoint.basePath = basePath.toStdString();
        endpoint.serverFingerprint = serverFp.toStdString();
        session_->connectServer(endpoint);
        session_->subscribe(days);
    } catch (const std::exception& e) {
        emit connectionChanged(false, QString::fromUtf8(e.what()));
        emit actionFailed(QString::fromUtf8(e.what()));
        return;
    }
    emit connectionChanged(true, "active");
    emit actionOk("Connected and subscribed.");
    ensureSyncTimer();
    sync();
}

void SessionWorker::sync()
{
    if (!session_ || !session_->isConnected()) {
        return;
    }
    std::vector<IncomingMessage> messages;
    try {
        messages = session_->sync();
    } catch (const std::exception&) {
        return;  // transient (server momentarily unreachable); next tick retries
    }
    for (const IncomingMessage& m : messages) {
        QVariantMap map;
        map["peer"] = QString::fromStdString(m.fromFingerprint);
        map["type"] = QString::fromStdString(m.contentType);
        map["text"] = QString::fromStdString(m.text);
        map["rawType"] = QString::fromStdString(m.rawType);
        map["established"] = m.establishedContact;
        map["attName"] = QString::fromStdString(m.attachmentName);
        map["attMime"] = QString::fromStdString(m.attachmentMime);
        map["attSize"] = static_cast<qint64>(m.attachmentSize);
        map["attRef"] = QString::fromStdString(m.attachmentRef);
        map["attKey"] = QString::fromStdString(m.attachmentKeyB64);
        map["messageId"] = QString::fromStdString(m.messageId);
        map["ref"] = QString::fromStdString(m.refId);
        emit messageReceived(map);
    }
    QStringList fps;
    for (const std::string& fp : session_->contactFingerprints()) {
        fps << QString::fromStdString(fp);
    }
    emit contactsRefreshed(fps);
}

void SessionWorker::sendText(
    const QString& peer, const QString& text, qint64 localId, const QString& protocolId)
{
    try {
        session_->sendMessage(peer.toStdString(), text.toStdString(), protocolId.toStdString(),
            [this, localId]() { emit sendProgress(localId, DeliveryStatus::AtSenderServer); });
        emit sendResult(localId, true, {});  // advances to AtRecipientServer
    } catch (const std::exception& e) {
        emit sendResult(localId, false, QString::fromUtf8(e.what()));
    }
}

void SessionWorker::sendFile(
    const QString& peer, const QString& localPath, qint64 localId, const QString& protocolId)
{
    try {
        session_->sendFile(peer.toStdString(), localPath.toStdString(), protocolId.toStdString(),
            [this, localId]() { emit sendProgress(localId, DeliveryStatus::AtSenderServer); });
        emit sendResult(localId, true, {});  // advances to AtRecipientServer
    } catch (const std::exception& e) {
        emit sendResult(localId, false, QString::fromUtf8(e.what()));
    }
}

void SessionWorker::sendReceipt(const QString& peer, const QString& refId)
{
    try {
        session_->sendReceipt(peer.toStdString(), refId.toStdString());
    } catch (const std::exception&) {
        // A failed receipt is non-fatal; the sender simply stays at "yellow".
    }
}

void SessionWorker::addByInvite(const QString& uri, const QString& intro)
{
    try {
        session_->addByInvite(uri.toStdString(), intro.toStdString());
        emit actionOk("Contact request sent.");
        sync();
    } catch (const std::exception& e) {
        emit actionFailed(QString::fromUtf8(e.what()));
    }
}

void SessionWorker::addByUsername(const QString& alias, const QString& intro)
{
    try {
        session_->addByUsername(alias.toStdString(), intro.toStdString());
        emit actionOk("Contact request sent.");
        sync();
    } catch (const std::exception& e) {
        emit actionFailed(QString::fromUtf8(e.what()));
    }
}

void SessionWorker::addByFingerprint(const QString& fingerprint, const QString& intro)
{
    try {
        session_->sendContactRequest(fingerprint.toStdString(), intro.toStdString());
        emit actionOk("Contact request sent.");
        sync();
    } catch (const std::exception& e) {
        emit actionFailed(QString::fromUtf8(e.what()));
    }
}

void SessionWorker::registerAlias(const QString& alias)
{
    try {
        session_->registerAlias(alias.toStdString());
        emit actionOk("Username registered.");
    } catch (const std::exception& e) {
        emit actionFailed(QString::fromUtf8(e.what()));
    }
}

void SessionWorker::requestInvite()
{
    try {
        emit inviteReady(QString::fromStdString(session_->inviteUri()));
    } catch (const std::exception& e) {
        emit actionFailed(QString::fromUtf8(e.what()));
    }
}

void SessionWorker::saveAttachment(
    const QString& ref, const QString& key, const QString& destPath)
{
    try {
        session_->saveAttachment(ref.toStdString(), key.toStdString(), destPath.toStdString());
        emit actionOk("Saved.");
    } catch (const std::exception& e) {
        emit actionFailed(QString::fromUtf8(e.what()));
    }
}

void SessionWorker::exportProfile(const QString& path, const QString& password)
{
    try {
        session_->exportState(path.toStdString(), password.toStdString());
        emit actionOk("Backup exported.");
    } catch (const std::exception& e) {
        emit actionFailed(QString::fromUtf8(e.what()));
    }
}

// ============================ SessionController ============================

SessionController::SessionController(QObject* parent)
    : QObject(parent)
{
    worker_ = new SessionWorker();
    worker_->moveToThread(&thread_);
    connect(&thread_, &QThread::finished, worker_, &QObject::deleteLater);

    // Commands → worker (queued across threads).
    connect(this, &SessionController::requestOpen, worker_, &SessionWorker::openProfile);
    connect(this, &SessionController::requestConnect, worker_, &SessionWorker::connectAndSubscribe);
    connect(this, &SessionController::requestSendText, worker_, &SessionWorker::sendText);
    connect(this, &SessionController::requestSendFile, worker_, &SessionWorker::sendFile);
    connect(this, &SessionController::requestSendReceipt, worker_, &SessionWorker::sendReceipt);
    connect(this, &SessionController::requestAddByInvite, worker_, &SessionWorker::addByInvite);
    connect(this, &SessionController::requestAddByUsername, worker_, &SessionWorker::addByUsername);
    connect(this, &SessionController::requestAddByFingerprint, worker_,
        &SessionWorker::addByFingerprint);
    connect(this, &SessionController::requestRegisterAlias, worker_,
        &SessionWorker::registerAlias);
    connect(this, &SessionController::requestInviteSig, worker_, &SessionWorker::requestInvite);
    connect(this, &SessionController::requestSaveAttachment, worker_,
        &SessionWorker::saveAttachment);
    connect(this, &SessionController::requestExport, worker_, &SessionWorker::exportProfile);

    // Results → controller (queued).
    connect(worker_, &SessionWorker::opened, this, &SessionController::onOpened);
    connect(worker_, &SessionWorker::openFailed, this, &SessionController::openFailed);
    connect(worker_, &SessionWorker::connectionChanged, this,
        &SessionController::onConnectionChanged);
    connect(worker_, &SessionWorker::messageReceived, this,
        &SessionController::onMessageReceived);
    connect(worker_, &SessionWorker::contactsRefreshed, this,
        [this](const QStringList& fps) {
            QVector<ContactRow> rows;
            for (const QString& fp : fps) {
                rows.push_back(ContactRow{fp, fp, store_.lastText(fp), store_.lastTime(fp), 0});
            }
            contacts_.setContacts(std::move(rows));
        });
    connect(worker_, &SessionWorker::sendProgress, this, &SessionController::onSendProgress);
    connect(worker_, &SessionWorker::sendResult, this, &SessionController::onSendResult);
    connect(worker_, &SessionWorker::actionOk, this, &SessionController::actionOk);
    connect(worker_, &SessionWorker::actionFailed, this, &SessionController::actionFailed);
    connect(worker_, &SessionWorker::inviteReady, this, &SessionController::inviteReady);

    thread_.start();
}

SessionController::~SessionController()
{
    thread_.quit();
    thread_.wait();
}

void SessionController::open(const QString& dir, const QString& profileId, const QString& passphrase)
{
    profileId_ = profileId;
    store_.open(profileId, dir + "/transcript.db");
    emit requestOpen(dir, passphrase);
}

void SessionController::connectServer(
    const QString& host, int port, const QString& basePath, const QString& serverFp)
{
    emit requestConnect(host, port, basePath, serverFp, 14);
}

void SessionController::openConversation(const QString& peer)
{
    activePeer_ = peer;
    emit activePeerChanged();
    conversation_.setMessages(store_.messagesFor(peer));
    contacts_.clearUnread(peer);
}

QString SessionController_genProtocolId()
{
    return QString::number(QRandomGenerator::global()->generate64(), 16);
}

void SessionController::sendText(const QString& text)
{
    if (activePeer_.isEmpty() || text.isEmpty()) {
        return;
    }
    StoredMessage m;
    m.peer = activePeer_;
    m.outgoing = true;
    m.type = "text";
    m.protocolId = SessionController_genProtocolId();
    m.text = text;
    m.ts = nowSeconds();
    m.status = DeliveryStatus::Sending;
    m.id = store_.append(m);
    statusById_[m.id] = DeliveryStatus::Sending;
    conversation_.appendMessage(m);
    contacts_.touch(activePeer_, {}, text, m.ts, false);
    emit requestSendText(activePeer_, text, m.id, m.protocolId);
}

void SessionController::sendFile(const QString& fileUrl)
{
    if (activePeer_.isEmpty()) {
        return;
    }
    const QString localPath = QUrl(fileUrl).toLocalFile();
    if (localPath.isEmpty()) {
        return;
    }
    StoredMessage m;
    m.peer = activePeer_;
    m.outgoing = true;
    m.type = "file";
    m.protocolId = SessionController_genProtocolId();
    m.attName = QUrl(fileUrl).fileName();
    m.ts = nowSeconds();
    m.status = 0;
    m.id = store_.append(m);
    statusById_[m.id] = 0;
    conversation_.appendMessage(m);
    contacts_.touch(activePeer_, {}, "[file] " + m.attName, m.ts, false);
    emit requestSendFile(activePeer_, localPath, m.id, m.protocolId);
}

void SessionController::addByInvite(const QString& uri, const QString& intro)
{
    emit requestAddByInvite(uri, intro);
}

void SessionController::addByUsername(const QString& alias, const QString& intro)
{
    emit requestAddByUsername(alias, intro);
}

void SessionController::addByFingerprint(const QString& fingerprint, const QString& intro)
{
    emit requestAddByFingerprint(fingerprint, intro);
}

void SessionController::registerAlias(const QString& alias)
{
    emit requestRegisterAlias(alias);
}

void SessionController::requestInvite()
{
    emit requestInviteSig();
}

void SessionController::saveAttachment(
    const QString& ref, const QString& key, const QString& fileUrl)
{
    const QString localPath = QUrl(fileUrl).toLocalFile();
    if (!localPath.isEmpty()) {
        emit requestSaveAttachment(ref, key, localPath);
    }
}

void SessionController::exportProfile(const QString& fileUrl, const QString& password)
{
    const QString localPath = QUrl(fileUrl).toLocalFile();
    if (!localPath.isEmpty()) {
        emit requestExport(localPath, password);
    }
}

QString SessionController::shortFingerprint(const QString& fp) const
{
    if (fp.size() <= 14) {
        return fp;
    }
    return fp.left(8) + "…" + fp.right(4);
}

void SessionController::onOpened(const QString& fingerprint, const QString& displayName,
    bool connected, const QString& subscriptionText)
{
    fingerprint_ = fingerprint;
    displayName_ = displayName;
    connected_ = connected;
    subscriptionText_ = subscriptionText;
    emit identityChanged();
    emit connectedChanged();
}

void SessionController::onConnectionChanged(bool connected, const QString& subscriptionText)
{
    connected_ = connected;
    subscriptionText_ = subscriptionText;
    emit connectedChanged();
}

void SessionController::onMessageReceived(const QVariantMap& message)
{
    const QString peer = message.value("peer").toString();
    const QString type = message.value("type").toString();

    // A delivery receipt acknowledges one of our sent messages: mark it
    // "green" (received by the peer's client). Not shown as a message.
    if (type == "receipt") {
        const qint64 localId = store_.idForProtocol(message.value("ref").toString());
        if (localId != 0) {
            bumpStatus(localId, DeliveryStatus::Delivered);
        }
        return;
    }

    StoredMessage m;
    m.peer = peer;
    m.outgoing = false;
    m.type = type;
    m.protocolId = message.value("messageId").toString();
    m.text = message.value("text").toString();
    m.attName = message.value("attName").toString();
    m.attMime = message.value("attMime").toString();
    m.attSize = message.value("attSize").toLongLong();
    m.attRef = message.value("attRef").toString();
    m.attKey = message.value("attKey").toString();
    m.ts = nowSeconds();
    m.status = DeliveryStatus::Received;  // incoming; no indicator rendered
    m.id = store_.append(m);

    if (peer == activePeer_) {
        conversation_.appendMessage(m);
    }
    QString preview = m.text;
    if (preview.isEmpty() && !m.attName.isEmpty()) {
        preview = "[" + type + "] " + m.attName;
    }
    contacts_.touch(peer, peer, preview, m.ts, peer != activePeer_);

    // Send a delivery receipt back (the "green" signal) when enabled, for
    // user-visible content only — never for control messages.
    if (sendReceipts_ && !m.protocolId.isEmpty()
        && (type == "text" || type == "file" || type == "photo" || type == "audio"
            || type == "voice")) {
        emit requestSendReceipt(peer, m.protocolId);
    }
}

void SessionController::bumpStatus(qint64 localId, int status)
{
    // Never downgrade (e.g. "yellow" arriving after "green"); failed is terminal.
    const int current = statusById_.value(localId, DeliveryStatus::Sending);
    if (status != DeliveryStatus::Failed && status <= current) {
        return;
    }
    statusById_[localId] = status;
    store_.updateStatus(localId, status);
    conversation_.setStatusForId(localId, status);
}

void SessionController::onSendProgress(qint64 localId, int state)
{
    bumpStatus(localId, state);  // AtSenderServer (our own server accepted it)
}

void SessionController::onSendResult(qint64 localId, bool ok, const QString& error)
{
    // States advance as fast as the real events occur — no artificial delay.
    // grey↔yellow is only distinguishable when there is a real hop between two
    // distinct servers; on a same-server delivery they coincide, honestly.
    bumpStatus(localId, ok ? DeliveryStatus::AtRecipientServer : DeliveryStatus::Failed);
    if (!ok) {
        emit actionFailed(error);
    }
}

}  // namespace bazarish::app
