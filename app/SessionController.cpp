// Bazarish project (c) 2026
#include "SessionController.hpp"

#include "DeliveryStatus.hpp"
#include "Invite.hpp"
#include "QtAudioIo.hpp"
#include "QtVideoIo.hpp"
#include "Session.hpp"

#include <QRandomGenerator>
#include <QTimer>
#include <QUrl>

#include <ctime>
#include <exception>
#include <fstream>
#include <iterator>
#include <memory>

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
    // Real microphone/speaker for calls (Qt Multimedia). The factories run on
    // this worker thread when a call starts, so the QAudio objects live here.
    session_->setAudioBackend(
        []() -> std::unique_ptr<bazarish::AudioSource> { return std::make_unique<QtAudioSource>(); },
        []() -> std::unique_ptr<bazarish::AudioSink> { return std::make_unique<QtAudioSink>(); });
    // Real camera/display for video calls. Like the audio factories these run on
    // this worker thread when a call starts; frames reach the GUI-thread
    // presenters through queued invocation inside the backend.
    VideoPresenter* const local = localPreview_;
    VideoPresenter* const remote = remotePreview_;
    session_->setVideoBackend(
        [local]() -> std::unique_ptr<bazarish::VideoSource> {
            return std::make_unique<QtVideoSource>(local);
        },
        [remote]() -> std::unique_ptr<bazarish::VideoSink> {
            return std::make_unique<QtVideoSink>(remote);
        });
    const bool connected = session_->isConnected();
    emit opened(QString::fromStdString(session_->fingerprint()),
        QString::fromStdString(session_->displayName()), connected,
        connected ? "connected" : "");
    QStringList fps;
    for (const std::string& fp : session_->contactFingerprints()) {
        fps << QString::fromStdString(fp);
    }
    emit contactsRefreshed(fps);
    emitGroups();
    emitFacadeInfo();
    if (connected) {
        ensureSyncTimer();
        sync();
    }
}

void SessionWorker::emitFacadeInfo()
{
    if (!session_ || !session_->isConnected()) {
        return;
    }
    QStringList configured;
    for (const std::string& url : session_->facadeUrls()) {
        configured << QString::fromStdString(url);
    }
    emit facadeInfo(QString::fromStdString(session_->activeFacadeUrl()), configured,
        QString::fromStdString(session_->endpoint().serverFingerprint));
}

void SessionWorker::connectAndSubscribe(
    const QStringList& facadeUrls, const QString& serverFp, int days)
{
    if (!session_) {
        return;
    }
    try {
        ServerEndpoint endpoint;
        endpoint.serverFingerprint = serverFp.toStdString();
        for (const QString& url : facadeUrls) {
            const QString trimmed = url.trimmed();
            if (!trimmed.isEmpty()) {
                endpoint.facades.push_back(
                    bazarish::client::parseFacadeUrl(trimmed.toStdString()));
            }
        }
        if (endpoint.facades.empty()) {
            throw std::runtime_error("enter at least one facade URL");
        }
        session_->connectServer(endpoint);
        session_->subscribe(days);
    } catch (const std::exception& e) {
        const QString reason = QString::fromUtf8(e.what());
        emit connectionChanged(false, reason);
        // A refused subscribe is usually "this key is not registered yet". Fetch
        // the server's onboarding message + registration links and surface them
        // in a persistent dialog the user can copy from, instead of a transient
        // toast. Fall back to the plain error if the server has no portal info.
        try {
            const bazarish::client::PortalInfo info = session_->serverPortalInfo();
            if (!info.message.empty() || !info.links.empty()) {
                QStringList links;
                for (const std::string& link : info.links) {
                    links << QString::fromStdString(link);
                }
                emit serverHello(reason, QString::fromStdString(info.message), links);
                return;
            }
        } catch (const std::exception&) {
            // No portal info reachable; fall through to the plain error.
        }
        emit actionFailed(reason);
        return;
    }
    emit connectionChanged(true, "active");
    emit actionOk("Connected and subscribed.");
    emitFacadeInfo();
    ensureSyncTimer();
    sync();
}

void SessionWorker::updateFacades(const QStringList& facadeUrls)
{
    if (!session_ || !session_->isConnected()) {
        return;
    }
    try {
        ServerEndpoint endpoint;
        endpoint.serverFingerprint = session_->endpoint().serverFingerprint;
        for (const QString& url : facadeUrls) {
            const QString trimmed = url.trimmed();
            if (!trimmed.isEmpty()) {
                endpoint.facades.push_back(
                    bazarish::client::parseFacadeUrl(trimmed.toStdString()));
            }
        }
        if (endpoint.facades.empty()) {
            throw std::runtime_error("enter at least one facade URL");
        }
        session_->connectServer(endpoint);
        emit actionOk("Facades updated.");
        emitFacadeInfo();
        sync();
    } catch (const std::exception& e) {
        emit actionFailed(QString::fromUtf8(e.what()));
    }
}

void SessionWorker::setSyncEnabled(bool on)
{
    if (on) {
        ensureSyncTimer();
        sync();
    } else if (syncTimer_ != nullptr) {
        syncTimer_->stop();
    }
}

void SessionWorker::sync()
{
    if (!session_ || !session_->isConnected()) {
        return;
    }
    std::vector<IncomingMessage> messages;
    try {
        messages = session_->sync();
        emit syncReachable(true);
    } catch (const std::exception&) {
        emit syncReachable(false);
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
        map["keyboard"] = QString::fromStdString(m.keyboardJson);
        map["groupId"] = QString::fromStdString(m.groupId);
        map["groupName"] = QString::fromStdString(m.groupName);
        map["sender"] = QString::fromStdString(m.fromFingerprint);
        map["messageId"] = QString::fromStdString(m.messageId);
        map["ref"] = QString::fromStdString(m.refId);
        emit messageReceived(map);
    }
    QStringList fps;
    for (const std::string& fp : session_->contactFingerprints()) {
        fps << QString::fromStdString(fp);
    }
    emit contactsRefreshed(fps);
    emitGroups();
    emitFacadeInfo();
    // Surface any call state change picked up this sync (a new invite, the peer
    // accepting, or a hang-up) and refresh live media stats.
    emitCallState();
}

void SessionWorker::emitCallState()
{
    if (!session_) {
        return;
    }
    const Session::CallInfo call = session_->currentCall();
    emit callStateChanged(static_cast<int>(call.state), QString::fromStdString(call.peerFingerprint),
        QString::fromStdString(call.callId), call.muted, call.video, call.cameraOn);
}

void SessionWorker::setVideoPresenters(VideoPresenter* local, VideoPresenter* remote)
{
    localPreview_ = local;
    remotePreview_ = remote;
}

void SessionWorker::startCall(const QString& peer, const bool video)
{
    if (!session_) {
        return;
    }
    try {
        if (video) {
            session_->startVideoCall(peer.toStdString());
        } else {
            session_->startAudioCall(peer.toStdString());
        }
    } catch (const std::exception& e) {
        emit actionFailed(QString::fromUtf8(e.what()));
    }
    emitCallState();
}

void SessionWorker::acceptCall(const QString& callId)
{
    if (!session_) {
        return;
    }
    try {
        session_->acceptCall(callId.toStdString());
    } catch (const std::exception& e) {
        emit actionFailed(QString::fromUtf8(e.what()));
    }
    emitCallState();
}

void SessionWorker::declineCall(const QString& callId)
{
    if (!session_) {
        return;
    }
    try {
        session_->declineCall(callId.toStdString());
    } catch (const std::exception&) {
    }
    emitCallState();
}

void SessionWorker::endCall()
{
    if (!session_) {
        return;
    }
    try {
        session_->endCall();
    } catch (const std::exception&) {
    }
    emitCallState();
}

void SessionWorker::setCallMuted(const bool muted)
{
    if (!session_) {
        return;
    }
    session_->setCallMuted(muted);
    emitCallState();
}

void SessionWorker::setCameraEnabled(const bool enabled)
{
    if (!session_) {
        return;
    }
    session_->setCameraEnabled(enabled);
    emitCallState();
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

void SessionWorker::sendCallback(const QString& peer, const QString& data, const QString& ref)
{
    try {
        session_->sendCallback(peer.toStdString(), data.toStdString(), ref.toStdString());
    } catch (const std::exception& e) {
        emit actionFailed(QString::fromUtf8(e.what()));
    }
}

void SessionWorker::sendCommand(const QString& peer, const QString& command, const QString& args)
{
    try {
        session_->sendCommand(peer.toStdString(), command.toStdString(), args.toStdString());
    } catch (const std::exception& e) {
        emit actionFailed(QString::fromUtf8(e.what()));
    }
}

void SessionWorker::sendEdit(const QString& peer, const QString& refId, const QString& text)
{
    try {
        // A user edit replaces text only; the (empty) keyboard clears none here
        // because user messages carry no keyboard.
        session_->sendEdit(peer.toStdString(), refId.toStdString(), text.toStdString());
    } catch (const std::exception& e) {
        emit actionFailed(QString::fromUtf8(e.what()));
    }
}

void SessionWorker::emitGroups()
{
    if (!session_) {
        return;
    }
    QStringList ids;
    QStringList names;
    for (const std::string& id : session_->groupIds()) {
        ids << QString::fromStdString(id);
        names << QString::fromStdString(session_->groupName(id));
    }
    emit groupsRefreshed(ids, names);
}

void SessionWorker::createGroup(const QString& name, const QStringList& memberFps)
{
    try {
        std::vector<std::string> members;
        members.reserve(memberFps.size());
        for (const QString& fp : memberFps) {
            members.push_back(fp.toStdString());
        }
        const std::string groupId = session_->createGroup(name.toStdString(), members);
        emit groupCreated(QString::fromStdString(groupId), name);
        emitGroups();
    } catch (const std::exception& e) {
        emit actionFailed(QString::fromUtf8(e.what()));
    }
}

void SessionWorker::sendGroupText(const QString& groupId, const QString& text, qint64 localId)
{
    try {
        session_->sendGroupMessage(groupId.toStdString(), text.toStdString());
        emit sendResult(localId, true, {});
    } catch (const std::exception& e) {
        emit sendResult(localId, false, QString::fromUtf8(e.what()));
    }
}

void SessionWorker::fetchGroupMembers(const QString& groupId)
{
    if (!session_) {
        return;
    }
    QStringList members;
    for (const std::string& fp : session_->groupMemberFingerprints(groupId.toStdString())) {
        members << QString::fromStdString(fp);
    }
    emit groupMembersReady(groupId, members, session_->isGroupAdmin(groupId.toStdString()));
}

void SessionWorker::addGroupMembers(const QString& groupId, const QStringList& fps)
{
    try {
        std::vector<std::string> members;
        for (const QString& fp : fps) {
            members.push_back(fp.toStdString());
        }
        session_->addGroupMembers(groupId.toStdString(), members);
        emit actionOk("Members added.");
        emitGroups();
        fetchGroupMembers(groupId);
    } catch (const std::exception& e) {
        emit actionFailed(QString::fromUtf8(e.what()));
    }
}

void SessionWorker::removeGroupMember(const QString& groupId, const QString& fp)
{
    try {
        session_->removeGroupMember(groupId.toStdString(), fp.toStdString());
        emit actionOk("Member removed.");
        emitGroups();
        fetchGroupMembers(groupId);
    } catch (const std::exception& e) {
        emit actionFailed(QString::fromUtf8(e.what()));
    }
}

void SessionWorker::leaveGroup(const QString& groupId)
{
    try {
        session_->leaveGroup(groupId.toStdString());
        emit actionOk("Left the group.");
        emitGroups();
    } catch (const std::exception& e) {
        emit actionFailed(QString::fromUtf8(e.what()));
    }
}

void SessionWorker::addByInvite(const QString& uri, const QString& intro)
{
    try {
        const std::string fingerprint = session_->addByInvite(uri.toStdString(), intro.toStdString());
        // Surface the fingerprint for out-of-band verification (safety-number style).
        emit actionOk(QString::fromStdString("Contact request sent. Verify fingerprint: " + fingerprint));
        sync();
    } catch (const std::exception& e) {
        emit actionFailed(QString::fromUtf8(e.what()));
    }
}

void SessionWorker::addByUsername(const QString& alias, const QString& intro)
{
    try {
        const std::string fingerprint = session_->addByUsername(alias.toStdString(), intro.toStdString());
        // The alias->fingerprint binding is the one residual trust of the name
        // path; surface the resolved fingerprint for out-of-band verification.
        emit actionOk(QString::fromStdString("Contact request sent. Verify fingerprint: " + fingerprint));
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

void SessionWorker::requestInvite()
{
    try {
        emit inviteReady(QString::fromStdString(session_->inviteUri()));
    } catch (const std::exception& e) {
        emit actionFailed(QString::fromUtf8(e.what()));
    }
}

void SessionWorker::signLogin(const QString& challenge)
{
    if (!session_) {
        emit actionFailed(QStringLiteral("no profile open"));
        return;
    }
    try {
        emit loginSigned(QString::fromStdString(session_->signLogin(challenge.toStdString())));
    } catch (const std::exception& e) {
        emit actionFailed(QString::fromUtf8(e.what()));
    }
}

void SessionWorker::refreshI2pStatus()
{
    if (!session_) {
        return;
    }
    const bool hasKey = session_->hasI2pDestination();
    const QString address
        = hasKey ? QString::fromStdString(session_->i2pAddress() + ".b32.i2p") : QString();
    bool enabled = false;
    bool active = false;
    qint64 paidThrough = 0;
    QString summary;
    try {
        const bazarish::client::I2pDestStatus s = session_->i2pDestStatus();
        enabled = s.enabled;
        active = s.active;
        paidThrough = static_cast<qint64>(s.paidThrough);
        if (enabled && active) {
            summary = QStringLiteral("On — your personal destination is live.");
        } else if (enabled) {
            summary = QStringLiteral(
                "On but offline — top up to restore it, or turn it off to use the pool.");
        } else if (hasKey) {
            summary = QStringLiteral("Off — using the shared pool address (personal key ready).");
        } else {
            summary = QStringLiteral("Off — using the shared pool address.");
        }
    } catch (const std::exception&) {
        // Not connected (or the node lacks the endpoint): show what we know.
        summary = hasKey ? QStringLiteral("Personal key ready; connect to manage it.")
                         : QStringLiteral("Using the shared pool address.");
    }
    emit i2pStatus(hasKey, enabled, active, address, summary, paidThrough);
}

void SessionWorker::generatePersonalKey()
{
    if (!session_) {
        return;
    }
    try {
        session_->ensureI2pDestination();
        emit actionOk("Personal I2P key created.");
    } catch (const std::exception& e) {
        emit actionFailed(QString::fromUtf8(e.what()));
    }
    refreshI2pStatus();
}

void SessionWorker::loadPersonalKey(const QString& path)
{
    if (!session_) {
        return;
    }
    try {
        std::ifstream in(path.toStdString(), std::ios::binary);
        if (!in) {
            throw std::runtime_error("cannot open key file");
        }
        const bazarish::Bytes dat(
            (std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        session_->loadI2pDestination(dat);
        emit actionOk("Personal I2P key loaded.");
    } catch (const std::exception& e) {
        emit actionFailed(QString::fromUtf8(e.what()));
    }
    refreshI2pStatus();
}

void SessionWorker::deletePersonalKey()
{
    if (!session_) {
        return;
    }
    try {
        session_->deleteI2pDestination();
        emit actionOk("Personal I2P key deleted.");
    } catch (const std::exception& e) {
        emit actionFailed(QString::fromUtf8(e.what()));
    }
    refreshI2pStatus();
}

void SessionWorker::enablePersonalDest()
{
    if (!session_) {
        return;
    }
    try {
        if (session_->enableI2pDest(static_cast<std::int64_t>(std::time(nullptr)))) {
            emit actionOk("Personal I2P destination enabled.");
        } else {
            emit actionFailed("Insufficient balance — top up on the portal first.");
        }
    } catch (const std::exception& e) {
        emit actionFailed(QString::fromUtf8(e.what()));
    }
    refreshI2pStatus();
}

void SessionWorker::disablePersonalDest()
{
    if (!session_) {
        return;
    }
    try {
        session_->disableI2pDest();
        emit actionOk("Personal I2P destination disabled.");
    } catch (const std::exception& e) {
        emit actionFailed(QString::fromUtf8(e.what()));
    }
    refreshI2pStatus();
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

    // Video presenters live on the GUI thread (this object owns them); the video
    // backend on the worker thread renders into them via queued invocation. Set
    // before any openProfile so the injected backend can reach them.
    localVideo_ = new VideoPresenter(this);
    remoteVideo_ = new VideoPresenter(this);
    worker_->setVideoPresenters(localVideo_, remoteVideo_);

    // Commands -> worker (queued across threads).
    connect(this, &SessionController::requestOpen, worker_, &SessionWorker::openProfile);
    connect(this, &SessionController::requestConnect, worker_, &SessionWorker::connectAndSubscribe);
    connect(this, &SessionController::requestUpdateFacades, worker_, &SessionWorker::updateFacades);
    connect(this, &SessionController::requestSendText, worker_, &SessionWorker::sendText);
    connect(this, &SessionController::requestSendFile, worker_, &SessionWorker::sendFile);
    connect(this, &SessionController::requestSendReceipt, worker_, &SessionWorker::sendReceipt);
    connect(this, &SessionController::requestSendCallback, worker_, &SessionWorker::sendCallback);
    connect(this, &SessionController::requestSendCommand, worker_, &SessionWorker::sendCommand);
    connect(this, &SessionController::requestSendEdit, worker_, &SessionWorker::sendEdit);
    connect(this, &SessionController::requestCreateGroup, worker_, &SessionWorker::createGroup);
    connect(this, &SessionController::requestSendGroupText, worker_, &SessionWorker::sendGroupText);
    connect(this, &SessionController::requestAddGroupMembers, worker_,
        &SessionWorker::addGroupMembers);
    connect(this, &SessionController::requestRemoveGroupMember, worker_,
        &SessionWorker::removeGroupMember);
    connect(this, &SessionController::requestLeaveGroup, worker_, &SessionWorker::leaveGroup);
    connect(this, &SessionController::requestFetchGroupMembers, worker_,
        &SessionWorker::fetchGroupMembers);
    connect(this, &SessionController::requestAddByInvite, worker_, &SessionWorker::addByInvite);
    connect(this, &SessionController::requestAddByUsername, worker_, &SessionWorker::addByUsername);
    connect(this, &SessionController::requestAddByFingerprint, worker_,
        &SessionWorker::addByFingerprint);
    connect(this, &SessionController::requestInviteSig, worker_, &SessionWorker::requestInvite);
    connect(this, &SessionController::requestSignLoginSig, worker_, &SessionWorker::signLogin);
    connect(this, &SessionController::requestSaveAttachment, worker_,
        &SessionWorker::saveAttachment);
    connect(this, &SessionController::requestExport, worker_, &SessionWorker::exportProfile);
    connect(this, &SessionController::requestSetSync, worker_, &SessionWorker::setSyncEnabled);
    connect(this, &SessionController::requestGeneratePersonalKey, worker_,
        &SessionWorker::generatePersonalKey);
    connect(this, &SessionController::requestLoadPersonalKey, worker_,
        &SessionWorker::loadPersonalKey);
    connect(this, &SessionController::requestDeletePersonalKey, worker_,
        &SessionWorker::deletePersonalKey);
    connect(this, &SessionController::requestEnablePersonalDest, worker_,
        &SessionWorker::enablePersonalDest);
    connect(this, &SessionController::requestDisablePersonalDest, worker_,
        &SessionWorker::disablePersonalDest);
    connect(this, &SessionController::requestRefreshI2pStatus, worker_,
        &SessionWorker::refreshI2pStatus);
    connect(this, &SessionController::requestStartCall, worker_, &SessionWorker::startCall);
    connect(this, &SessionController::requestAcceptCall, worker_, &SessionWorker::acceptCall);
    connect(this, &SessionController::requestDeclineCall, worker_, &SessionWorker::declineCall);
    connect(this, &SessionController::requestEndCall, worker_, &SessionWorker::endCall);
    connect(this, &SessionController::requestSetCallMuted, worker_, &SessionWorker::setCallMuted);
    connect(this, &SessionController::requestSetCameraEnabled, worker_,
        &SessionWorker::setCameraEnabled);

    // Results -> controller (queued).
    connect(worker_, &SessionWorker::opened, this, &SessionController::onOpened);
    connect(worker_, &SessionWorker::openFailed, this, &SessionController::openFailed);
    connect(worker_, &SessionWorker::connectionChanged, this,
        &SessionController::onConnectionChanged);
    connect(worker_, &SessionWorker::messageReceived, this,
        &SessionController::onMessageReceived);
    connect(worker_, &SessionWorker::contactsRefreshed, this,
        [this](const QStringList& fps) {
            contactFps_ = fps;
            rebuildChatList();
        });
    connect(worker_, &SessionWorker::groupsRefreshed, this, &SessionController::onGroupsRefreshed);
    connect(worker_, &SessionWorker::groupCreated, this, &SessionController::onGroupCreated);
    connect(worker_, &SessionWorker::groupMembersReady, this,
        &SessionController::onGroupMembersReady);
    connect(worker_, &SessionWorker::sendProgress, this, &SessionController::onSendProgress);
    connect(worker_, &SessionWorker::sendResult, this, &SessionController::onSendResult);
    connect(worker_, &SessionWorker::syncReachable, this, &SessionController::onSyncReachable);
    connect(worker_, &SessionWorker::facadeInfo, this, &SessionController::onFacadeInfo);
    connect(worker_, &SessionWorker::actionOk, this, &SessionController::actionOk);
    connect(worker_, &SessionWorker::actionFailed, this, &SessionController::actionFailed);
    connect(worker_, &SessionWorker::inviteReady, this, &SessionController::inviteReady);
    connect(worker_, &SessionWorker::loginSigned, this, &SessionController::loginSigned);
    connect(worker_, &SessionWorker::serverHello, this, &SessionController::serverHello);
    connect(worker_, &SessionWorker::i2pStatus, this, &SessionController::onI2pStatus);
    connect(worker_, &SessionWorker::callStateChanged, this,
        &SessionController::onCallStateChanged);

    // Keep the account-wide unread total in sync with the contacts model, so the
    // switcher badge updates even while this account is in the background.
    connect(&contacts_, &QAbstractItemModel::dataChanged, this,
        &SessionController::refreshUnreadTotal);
    connect(&contacts_, &QAbstractItemModel::rowsInserted, this,
        &SessionController::refreshUnreadTotal);
    connect(&contacts_, &QAbstractItemModel::modelReset, this,
        &SessionController::refreshUnreadTotal);

    thread_.start();
}

void SessionController::refreshUnreadTotal()
{
    const int total = contacts_.totalUnread();
    if (total != unreadTotal_) {
        unreadTotal_ = total;
        emit unreadTotalChanged();
    }
}

SessionController::~SessionController()
{
    thread_.quit();
    thread_.wait();
}

void SessionController::open(const QString& dir, const QString& profileId, const QString& passphrase)
{
    profileId_ = profileId;
    // The passphrase that unlocks the keys also seals the transcript at rest.
    store_.open(profileId, dir + "/transcript.db", passphrase);
    emit requestOpen(dir, passphrase);
}

void SessionController::connectServer(const QStringList& facadeUrls, const QString& serverFp)
{
    emit requestConnect(facadeUrls, serverFp, 14);
}

void SessionController::updateFacades(const QStringList& facadeUrls)
{
    emit requestUpdateFacades(facadeUrls);
}

void SessionController::onFacadeInfo(
    const QString& activeUrl, const QStringList& configured, const QString& serverFp)
{
    activeFacade_ = activeUrl;
    configuredFacades_ = configured;
    serverFp_ = serverFp;
    emit facadeInfoChanged();
}

QVariantMap SessionController::parseServerLink(const QString& uri) const
{
    QVariantMap result;
    try {
        const bazarish::client::ServerLink link
            = bazarish::client::decodeServerLink(uri.trimmed().toStdString());
        result["serverFp"] = QString::fromStdString(link.serverFingerprint);
        QStringList facades;
        for (const std::string& url : link.facadeUrls) {
            facades << QString::fromStdString(url);
        }
        result["facades"] = facades;
    } catch (const std::exception&) {
        // Malformed link: return an empty map (the caller checks).
    }
    return result;
}

void SessionController::openConversation(const QString& peer)
{
    activePeer_ = peer;
    emit activePeerChanged();
    conversation_.setMessages(store_.messagesFor(peer));
    contacts_.clearUnread(peer);
    // Load the member list for a group conversation (cleared for a 1:1 chat).
    activeGroupMembers_.clear();
    activeGroupAdmin_ = false;
    emit activeGroupChanged();
    if (groupIds_.contains(peer)) {
        emit requestFetchGroupMembers(peer);
    }
}

void SessionController::rebuildChatList()
{
    QVector<ContactRow> rows;
    for (const QString& fp : contactFps_) {
        rows.push_back(ContactRow{fp, fp, store_.lastText(fp), store_.lastTime(fp), 0, false});
    }
    for (const QString& gid : groupIds_) {
        rows.push_back(ContactRow{gid, groupNames_.value(gid, gid), store_.lastText(gid),
            store_.lastTime(gid), 0, true});
    }
    contacts_.setContacts(std::move(rows));
}

void SessionController::onGroupsRefreshed(const QStringList& ids, const QStringList& names)
{
    groupIds_ = ids;
    groupNames_.clear();
    for (int i = 0; i < ids.size() && i < names.size(); ++i) {
        groupNames_.insert(ids[i], names[i]);
    }
    rebuildChatList();
    // If the open group went away (we left it), close the conversation; otherwise
    // refresh its member list (membership may have changed).
    if (!activePeer_.isEmpty() && !groupIds_.contains(activePeer_)
        && !activeGroupMembers_.isEmpty()) {
        openConversation({});
    } else if (groupIds_.contains(activePeer_)) {
        emit requestFetchGroupMembers(activePeer_);
    }
}

void SessionController::onGroupCreated(const QString& groupId, const QString& name)
{
    if (!groupIds_.contains(groupId)) {
        groupIds_ << groupId;
    }
    groupNames_.insert(groupId, name);
    rebuildChatList();
    openConversation(groupId);
    emit actionOk("Group created.");
}

void SessionController::createGroup(const QString& name, const QStringList& memberFps)
{
    if (name.trimmed().isEmpty() || memberFps.isEmpty()) {
        return;
    }
    emit requestCreateGroup(name.trimmed(), memberFps);
}

bool SessionController::isGroup(const QString& id) const
{
    return groupIds_.contains(id);
}

QString SessionController::peerName(const QString& id) const
{
    if (groupIds_.contains(id)) {
        return groupNames_.value(id, id);
    }
    return shortFingerprint(id);
}

void SessionController::addGroupMembers(const QString& groupId, const QStringList& fps)
{
    if (!fps.isEmpty()) {
        emit requestAddGroupMembers(groupId, fps);
    }
}

void SessionController::removeGroupMember(const QString& groupId, const QString& fp)
{
    emit requestRemoveGroupMember(groupId, fp);
}

void SessionController::leaveGroup(const QString& groupId)
{
    emit requestLeaveGroup(groupId);
}

void SessionController::onGroupMembersReady(
    const QString& groupId, const QStringList& members, bool iAmAdmin)
{
    if (groupId == activePeer_) {
        activeGroupMembers_ = members;
        activeGroupAdmin_ = iAmAdmin;
        emit activeGroupChanged();
    }
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
    // Group conversation: fan out to all members via the group path.
    if (groupIds_.contains(activePeer_)) {
        StoredMessage gm;
        gm.peer = activePeer_;
        gm.outgoing = true;
        gm.type = "text";
        gm.sender = fingerprint_;
        gm.protocolId = SessionController_genProtocolId();
        gm.text = text;
        gm.ts = nowSeconds();
        gm.status = DeliveryStatus::Sending;
        gm.id = store_.append(gm);
        statusById_[gm.id] = DeliveryStatus::Sending;
        conversation_.appendMessage(gm);
        contacts_.touch(activePeer_, groupNames_.value(activePeer_), text, gm.ts, false, true);
        emit requestSendGroupText(activePeer_, text, gm.id);
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

void SessionController::sendCallback(const QString& data, const QString& refMsgId)
{
    if (activePeer_.isEmpty()) {
        return;
    }
    // A button press is silent in the transcript (inline-keyboard semantics):
    // the bot's reply is what appears. We just relay the callback.
    emit requestSendCallback(activePeer_, data, refMsgId);
}

void SessionController::sendCommand(const QString& command, const QString& args)
{
    if (activePeer_.isEmpty() || command.isEmpty()) {
        return;
    }
    emit requestSendCommand(activePeer_, command, args);
}

void SessionController::beginEdit(qint64 localId, const QString& protocolId, const QString& text)
{
    editing_ = true;
    editingLocalId_ = localId;
    editingProtocolId_ = protocolId;
    editingText_ = text;
    emit editingChanged();
}

void SessionController::commitEdit(const QString& newText)
{
    if (!editing_) {
        return;
    }
    const QString trimmed = newText.trimmed();
    // An empty edit, or no real change, just cancels.
    if (!trimmed.isEmpty() && trimmed != editingText_) {
        // Update our own copy in place (user messages carry no keyboard), then
        // tell the peer to update theirs.
        store_.editContent(editingLocalId_, trimmed, {});
        conversation_.editById(editingLocalId_, trimmed, {});
        contacts_.touch(activePeer_, {}, trimmed, nowSeconds(), false);
        emit requestSendEdit(activePeer_, editingProtocolId_, trimmed);
    }
    cancelEdit();
}

void SessionController::cancelEdit()
{
    if (!editing_) {
        return;
    }
    editing_ = false;
    editingLocalId_ = 0;
    editingProtocolId_.clear();
    editingText_.clear();
    emit editingChanged();
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

void SessionController::requestInvite()
{
    emit requestInviteSig();
}

void SessionController::signLogin(const QString& challenge)
{
    emit requestSignLoginSig(challenge);
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

void SessionController::generatePersonalKey()
{
    emit requestGeneratePersonalKey();
}

void SessionController::loadPersonalKey(const QString& fileUrl)
{
    const QString localPath = QUrl(fileUrl).toLocalFile();
    if (!localPath.isEmpty()) {
        emit requestLoadPersonalKey(localPath);
    }
}

void SessionController::deletePersonalKey()
{
    emit requestDeletePersonalKey();
}

void SessionController::enablePersonalDest()
{
    emit requestEnablePersonalDest();
}

void SessionController::disablePersonalDest()
{
    emit requestDisablePersonalDest();
}

void SessionController::refreshI2pStatus()
{
    emit requestRefreshI2pStatus();
}

void SessionController::onI2pStatus(const bool hasKey, const bool enabled, const bool active,
    const QString& address, const QString& summary, const qint64 paidThrough)
{
    i2pHasKey_ = hasKey;
    i2pEnabled_ = enabled;
    i2pActive_ = active;
    i2pAddress_ = address;
    i2pStatusText_ = summary;
    i2pPaidThrough_ = paidThrough;
    emit i2pStatusChanged();
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
    // A connected profile starts syncing on open, so it comes up online.
    if (online_ != connected) {
        online_ = connected;
        emit onlineChanged();
    }
}

void SessionController::onConnectionChanged(bool connected, const QString& subscriptionText)
{
    connected_ = connected;
    subscriptionText_ = subscriptionText;
    emit connectedChanged();
    if (online_ != connected) {
        online_ = connected;
        emit onlineChanged();
    }
}

void SessionController::goOnline()
{
    if (!online_) {
        online_ = true;
        emit onlineChanged();
    }
    emit requestSetSync(true);
}

void SessionController::goOffline()
{
    if (online_) {
        online_ = false;
        emit onlineChanged();
    }
    if (reachable_) {
        reachable_ = false;
        emit reachableChanged();
    }
    emit requestSetSync(false);
}

void SessionController::onSyncReachable(bool ok)
{
    if (reachable_ != ok) {
        reachable_ = ok;
        emit reachableChanged();
    }
}

void SessionController::onMessageReceived(const QVariantMap& message)
{
    const QString peer = message.value("peer").toString();
    const QString type = message.value("type").toString();

    // Call signalling drives the call screen via callStateChanged, never the
    // chat list.
    if (type.startsWith(QStringLiteral("call."))) {
        return;
    }

    // A delivery receipt acknowledges one of our sent messages: mark it
    // "green" (received by the peer's client). Not shown as a message.
    if (type == "receipt") {
        const qint64 localId = store_.idForProtocol(message.value("ref").toString());
        if (localId != 0) {
            bumpStatus(localId, DeliveryStatus::Delivered);
        }
        return;
    }

    // An in-place edit of a message this peer previously sent us: update it
    // where it sits instead of adding a new bubble. Scoped to incoming-from-peer
    // in the store, so a peer can only edit its own messages.
    if (type == "edit") {
        const qint64 localId
            = store_.idForIncomingProtocol(message.value("ref").toString(), peer);
        if (localId != 0) {
            const QString newText = message.value("text").toString();
            const QString newKeyboard = message.value("keyboard").toString();
            store_.editContent(localId, newText, newKeyboard);
            if (peer == activePeer_) {
                conversation_.editById(localId, newText, newKeyboard);
            }
            QString preview = newText;
            if (preview.isEmpty() && !newKeyboard.isEmpty()) {
                preview = "[interactive]";
            }
            contacts_.touch(peer, peer, preview, nowSeconds(), peer != activePeer_);
        }
        return;
    }

    // Added to a group: surface a friendly system line; the group itself is
    // added to the chat list by the group-list refresh.
    if (type == "group.invite") {
        const QString gid = message.value("groupId").toString();
        const QString gname = message.value("groupName").toString();
        StoredMessage sys;
        sys.peer = gid;
        sys.type = "system";
        sys.text = "You were added to \"" + gname + "\"";
        sys.ts = nowSeconds();
        sys.status = DeliveryStatus::Received;
        sys.id = store_.append(sys);
        if (gid == activePeer_) {
            conversation_.appendMessage(sys);
        }
        contacts_.touch(gid, gname, sys.text, sys.ts, gid != activePeer_, true);
        return;
    }
    if (type == "group.tokens" || type == "group.roster" || type == "group.leave") {
        return;  // group control; the chat list reflects the change
    }

    // A group content message is filed under the group, with its author recorded.
    const QString groupId = message.value("groupId").toString();
    const bool isGroupMsg = !groupId.isEmpty();
    const QString convKey = isGroupMsg ? groupId : peer;

    StoredMessage m;
    m.peer = convKey;
    m.outgoing = false;
    m.type = type;
    m.sender = isGroupMsg ? message.value("sender").toString() : QString();
    m.protocolId = message.value("messageId").toString();
    m.text = message.value("text").toString();
    m.attName = message.value("attName").toString();
    m.attMime = message.value("attMime").toString();
    m.attSize = message.value("attSize").toLongLong();
    m.attRef = message.value("attRef").toString();
    m.attKey = message.value("attKey").toString();
    m.keyboard = message.value("keyboard").toString();
    m.ts = nowSeconds();
    m.status = DeliveryStatus::Received;  // incoming; no indicator rendered
    m.id = store_.append(m);

    if (convKey == activePeer_) {
        conversation_.appendMessage(m);
    }
    QString preview = m.text;
    if (preview.isEmpty() && !m.attName.isEmpty()) {
        preview = "[" + type + "] " + m.attName;
    }
    if (isGroupMsg) {
        contacts_.touch(convKey, groupNames_.value(convKey), preview, m.ts,
            convKey != activePeer_, true);
    } else {
        contacts_.touch(peer, peer, preview, m.ts, peer != activePeer_);
    }

    // Send a delivery receipt back (the "green" signal) when enabled, for
    // user-visible content only - never for control or group messages (a group
    // receipt would have no single recipient mailbox to confirm to).
    if (sendReceipts_ && !isGroupMsg && !m.protocolId.isEmpty()
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
    // States advance as fast as the real events occur - no artificial delay.
    // grey<->yellow is only distinguishable when there is a real hop between two
    // distinct servers; on a same-server delivery they coincide, honestly.
    bumpStatus(localId, ok ? DeliveryStatus::AtRecipientServer : DeliveryStatus::Failed);
    if (!ok) {
        emit actionFailed(error);
    }
}

void SessionController::startCall(const QString& peer)
{
    const QString target = peer.isEmpty() ? activePeer_ : peer;
    if (target.isEmpty()) {
        return;
    }
    emit requestStartCall(target, false);
}

void SessionController::startVideoCall(const QString& peer)
{
    const QString target = peer.isEmpty() ? activePeer_ : peer;
    if (target.isEmpty()) {
        return;
    }
    emit requestStartCall(target, true);
}

QObject* SessionController::localVideo() const
{
    return localVideo_;
}

QObject* SessionController::remoteVideo() const
{
    return remoteVideo_;
}

void SessionController::acceptCall()
{
    emit requestAcceptCall(callId_);
}

void SessionController::declineCall()
{
    emit requestDeclineCall(callId_);
}

void SessionController::endCall()
{
    emit requestEndCall();
}

void SessionController::setCallMuted(const bool muted)
{
    emit requestSetCallMuted(muted);
}

void SessionController::setCameraEnabled(const bool enabled)
{
    emit requestSetCameraEnabled(enabled);
}

void SessionController::onCallStateChanged(const int state, const QString& peer,
    const QString& callId, const bool muted, const bool video, const bool cameraOn)
{
    static const char* const kNames[] = {"idle", "outgoing", "incoming", "active"};
    const QString name = (state >= 0 && state <= 3) ? QString::fromLatin1(kNames[state])
                                                    : QStringLiteral("idle");
    if (callState_ == name && callPeer_ == peer && callId_ == callId && callMuted_ == muted
        && callVideo_ == video && callCameraOn_ == cameraOn) {
        return;
    }
    callState_ = name;
    callPeer_ = peer;
    callId_ = callId;
    callMuted_ = muted;
    callVideo_ = video;
    callCameraOn_ = cameraOn;
    emit callChanged();
}

}  // namespace bazarish::app
