// Bazarish project (c) 2026
#include "SessionController.hpp"

#include "AvatarStore.hpp"
#include "DeliveryStatus.hpp"
#include "Invite.hpp"
#include "QtAudioIo.hpp"
#include "QtVideoIo.hpp"
#include "Session.hpp"

#include <QBuffer>
#include <QByteArray>
#include <QClipboard>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QMimeDatabase>
#include <QRandomGenerator>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>

#if defined(Q_OS_LINUX) && defined(BAZARISH_HAVE_QTDBUS)
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>
#endif

#include <algorithm>
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
// Unix milliseconds: the message display/order clock (sentAt is in ms).
qint64 nowMillis()
{
    return QDateTime::currentMSecsSinceEpoch();
}

// A received message whose sentAt is within this window of arrival is placed in
// sentAt order (repairing an out-of-order burst); an older arrival is appended at
// the end as new instead (docs-main Messages.md "Ordering and timestamps").
constexpr qint64 kReorderWindowMs = 5000;

// The order key (sort position) and display time for a received message.
// orderKey is sentAt when the message arrived within the reorder window, else the
// arrival time (so a long-delayed message lands at the end, not up in history);
// the display time is always the message's own sentAt when present.
struct Placement {
    qint64 displayTs = 0;
    qint64 orderKey = 0;
};
Placement placeReceived(qint64 sentAtMs, qint64 arrivalMs)
{
    const bool recent = sentAtMs > 0 && (arrivalMs - sentAtMs) <= kReorderWindowMs;
    return {sentAtMs > 0 ? sentAtMs : arrivalMs, recent ? sentAtMs : arrivalMs};
}

// How many messages a conversation loads per page (initial window and each
// older/newer step). Keeps even a huge dialog cheap to open and scroll.
constexpr int kPageSize = 50;
}  // namespace

// ============================ SessionWorker ============================

SessionWorker::~SessionWorker()
{
    // Stop in-flight downloads before session_ (which their tasks use) is torn
    // down. Cancel first so a parked fetch closes its stream and stops retrying,
    // then wait for the pool to drain.
    downloadsCancelled_.store(true);
    downloadPool_.waitForDone();
}

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
    // Bound concurrent downloads so a burst never spawns an unreasonable number of
    // throwaway I2P destinations at once.
    downloadPool_.setMaxThreadCount(3);
    // Drain any download still running against a previously opened session before
    // that session_ is replaced (its tasks hold a raw pointer to it).
    downloadsCancelled_.store(true);
    downloadPool_.waitForDone();
    downloadsCancelled_.store(false);
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
    emitContacts();
    // Seed the avatar store from disk: our own avatar plus every contact that has
    // one, so faces appear before any sync runs.
    {
        const bazarish::Bytes& own = session_->avatar();
        if (!own.empty()) {
            emit avatarReady(QString::fromStdString(session_->fingerprint()),
                QByteArray(reinterpret_cast<const char*>(own.data()),
                    static_cast<int>(own.size())));
        }
        for (const std::string& fp : session_->contactFingerprints()) {
            const bazarish::Bytes av = session_->contactAvatar(fp);
            if (!av.empty()) {
                emit avatarReady(QString::fromStdString(fp),
                    QByteArray(reinterpret_cast<const char*>(av.data()),
                        static_cast<int>(av.size())));
            }
        }
    }
    emitGroups();
    emitFacadeInfo();
    if (connected) {
        ensureSyncTimer();
        sync();
    }
}

void SessionWorker::emitContacts()
{
    if (!session_) {
        return;
    }
    QStringList fps;
    QStringList names;
    for (const std::string& fp : session_->contactFingerprints()) {
        fps << QString::fromStdString(fp);
        names << QString::fromStdString(session_->contactDisplayName(fp));
    }
    emit contactsRefreshed(fps, names);
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
    emit actionOk("Connected.");
    emitFacadeInfo();
    ensureSyncTimer();
    sync();
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
        // Avatar payloads and self-sync control messages never become chat
        // bubbles: route avatars to the store and drop the rest silently (the
        // chat list reflects name changes via the contactsRefreshed below).
        if (m.contentType == "avatar") {
            emit avatarReady(QString::fromStdString(m.fromFingerprint),
                QByteArray(m.avatarData.data(), static_cast<int>(m.avatarData.size())));
            continue;
        }
        if (m.contentType == "device.avatar") {
            emit avatarReady(QString::fromStdString(session_->fingerprint()),
                QByteArray(m.avatarData.data(), static_cast<int>(m.avatarData.size())));
            continue;
        }
        if (m.contentType == "device.contact-name" || m.contentType == "device.i2p-master") {
            continue;
        }
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
        map["sentAt"] = static_cast<qint64>(m.sentAt);
        emit messageReceived(map);
    }
    emitContacts();
    emitGroups();
    emitFacadeInfo();
    // Resolve any sends still in flight from earlier (late delivery or failure).
    reconcilePendingSends();
    // Surface any call state change picked up this sync (a new invite, the peer
    // accepting, or a hang-up) and refresh live media stats.
    emitCallState();
}

void SessionWorker::reconcilePendingSends()
{
    if (!session_ || pendingSends_.empty()) {
        return;
    }
    std::vector<qint64> resolved;
    for (const auto& [localId, attemptId] : pendingSends_) {
        const bazarish::client::Session::AttemptOutcome outcome = session_->pollAttempt(attemptId);
        if (outcome.status == "delivered") {
            emit sendProgress(localId, DeliveryStatus::AtRecipientServer);  // grey -> yellow
            resolved.push_back(localId);
        } else if (outcome.status == "failed") {
            // grey -> red, with the reason attached to the message.
            emit sendResult(localId, false,
                QString::fromStdString(outcome.errorMessage.empty() ? std::string("delivery failed")
                                                                     : outcome.errorMessage));
            resolved.push_back(localId);
        } else if (outcome.status == "unconfirmed" || outcome.status == "unknown") {
            // "unconfirmed": our server exhausted its retries without confirming
            // delivery, but the envelope may still have been stored (only its ack
            // was lost) - so this is NOT a failure. "unknown": the server forgot
            // the attempt (expired or it restarted). Either way stop tracking it;
            // the message stays grey and a read receipt can still turn it green.
            resolved.push_back(localId);
        }
        // "pending": still in flight; keep it for the next sync.
    }
    for (const qint64 localId : resolved) {
        pendingSends_.erase(localId);
    }
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
        // The callback fires "grey" the instant our own server accepts the
        // envelope; "delivered" (yellow) only when the server confirms the
        // recipient stored it. A still-pending delivery leaves the message grey
        // and is reconciled on later syncs via its attempt id.
        std::string attemptId;
        const bool delivered = session_->sendMessage(peer.toStdString(), text.toStdString(),
            protocolId.toStdString(),
            [this, localId]() { emit sendProgress(localId, DeliveryStatus::AtSenderServer); },
            &attemptId);
        if (delivered) {
            pendingSends_.erase(localId);
            emit sendProgress(localId, DeliveryStatus::AtRecipientServer);
        } else if (!attemptId.empty()) {
            pendingSends_[localId] = attemptId;
        }
        emit sendResult(localId, true, {});
    } catch (const std::exception& e) {
        pendingSends_.erase(localId);
        emit sendResult(localId, false, QString::fromUtf8(e.what()));
    }
}

void SessionWorker::sendFile(const QString& peer, const QString& localPath, qint64 localId,
    const QString& protocolId, qint64 ttlSeconds, int downloadCount)
{
    try {
        bazarish::client::BlobRetention retention;
        retention.ttlSeconds = ttlSeconds;
        if (downloadCount > 0) {
            retention.count = static_cast<std::uint32_t>(downloadCount);
        }
        std::string attemptId;
        const bool delivered = session_->sendFile(peer.toStdString(), localPath.toStdString(),
            protocolId.toStdString(),
            [this, localId]() { emit sendProgress(localId, DeliveryStatus::AtSenderServer); },
            &attemptId,
            [this, localId](std::uint64_t sent, std::uint64_t total) {
                emit uploadProgress(localId, static_cast<qint64>(sent), static_cast<qint64>(total));
            },
            retention);
        if (delivered) {
            pendingSends_.erase(localId);
            emit sendProgress(localId, DeliveryStatus::AtRecipientServer);
        } else if (!attemptId.empty()) {
            pendingSends_[localId] = attemptId;
        }
        emit sendResult(localId, true, {});
    } catch (const std::exception& e) {
        pendingSends_.erase(localId);
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

void SessionWorker::sendEdit(
    const QString& peer, const QString& refId, qint64 localId, const QString& text)
{
    try {
        // A user edit replaces text only (the empty keyboard carries nothing, as
        // user messages have none). It is delivery-tracked exactly like a fresh
        // send so the edited bubble's status reflects the edit, not the original:
        // grey on our server's accept, yellow on the recipient server's confirm,
        // and reconciled later via the attempt id.
        std::string attemptId;
        const bool delivered = session_->sendEdit(peer.toStdString(), refId.toStdString(),
            text.toStdString(), {},
            [this, localId]() { emit sendProgress(localId, DeliveryStatus::AtSenderServer); },
            &attemptId);
        if (delivered) {
            pendingSends_.erase(localId);
            emit sendProgress(localId, DeliveryStatus::AtRecipientServer);
        } else if (!attemptId.empty()) {
            pendingSends_[localId] = attemptId;
        }
        emit sendResult(localId, true, {});
    } catch (const std::exception& e) {
        pendingSends_.erase(localId);
        emit sendResult(localId, false, QString::fromUtf8(e.what()));
    }
}

void SessionWorker::sendDelete(const QString& peer, const QString& refId)
{
    try {
        // Reclaim the externalized blob (if this message had one) before telling
        // the peer to drop the message, so a deleted attachment leaves no trace.
        try {
            session_->unsend(refId.toStdString());
        } catch (const std::exception&) {
            // No blob recorded for this id (a plain text message), or the reclaim
            // failed: the blob also reclaims via its TTL. Not fatal to the delete.
        }
        session_->sendDelete(peer.toStdString(), refId.toStdString());
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
        // A group fan-out has no single recipient to confirm; treat a clean
        // send as handed off (yellow).
        emit sendProgress(localId, DeliveryStatus::AtRecipientServer);
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
        emit contactRequestSent(QString::fromStdString(fingerprint), intro);
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
        emit contactRequestSent(QString::fromStdString(fingerprint), intro);
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
        emit contactRequestSent(fingerprint, intro);
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

void SessionWorker::setAvatar(const QString& localPath)
{
    if (!session_) {
        return;
    }
    try {
        QImage img(localPath);
        if (img.isNull()) {
            emit actionFailed(QStringLiteral("Could not read the selected image."));
            return;
        }
        // Center-crop to a square, then downscale to a sane avatar resolution.
        const int side = std::min(img.width(), img.height());
        QImage square
            = img.copy((img.width() - side) / 2, (img.height() - side) / 2, side, side);
        constexpr int kDim = 256;
        if (square.width() > kDim) {
            square = square.scaled(kDim, kDim, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        }
        // Encode as JPEG within the 500 KB protocol cap, dropping quality - then,
        // as a last resort, resolution - until it fits.
        constexpr int kCap = 500 * 1024;
        const auto encode = [&square](int quality) {
            QByteArray out;
            QBuffer buffer(&out);
            buffer.open(QIODevice::WriteOnly);
            square.save(&buffer, "JPEG", quality);
            buffer.close();
            return out;
        };
        QByteArray bytes;
        int quality = 90;
        do {
            bytes = encode(quality);
            quality -= 15;
        } while (bytes.size() > kCap && quality >= 30);
        if (bytes.size() > kCap) {
            square = square.scaled(128, 128, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
            bytes = encode(80);
        }
        session_->setAvatar(bazarish::Bytes(bytes.begin(), bytes.end()), "image/jpeg");
        // Echo locally at once so our own avatar updates without waiting for a sync.
        emit avatarReady(QString::fromStdString(session_->fingerprint()), bytes);
        emit actionOk(QStringLiteral("Avatar updated."));
    } catch (const std::exception& e) {
        emit actionFailed(QString::fromUtf8(e.what()));
    }
}

void SessionWorker::renameContact(const QString& peer, const QString& name)
{
    if (!session_) {
        return;
    }
    try {
        session_->renameContact(peer.toStdString(), name.toStdString());
        emitContacts();  // reflect the new name in the chat list at once
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
    const QString& ref, const QString& key, const QString& destPath, qint64 token)
{
    // Run the download off the worker thread (on the pool) so a long or stalled
    // fetch never blocks sends, uploads or sync. The direct fetch uses its own
    // throwaway I2P endpoints; only the rare proxy fallback touches the (now
    // thread-safe) facade client. The task captures `this`, session_ and the
    // cancel flag, all kept alive until the pool is drained (see the destructor
    // and openProfile). Emits are skipped once cancelled, so a tearing-down
    // session is never signalled.
    Session* const session = session_.get();
    if (session == nullptr) {
        emit downloadFinished(token, false, QStringLiteral("no open session"));
        return;
    }
    const std::string refStd = ref.toStdString();
    const std::string keyStd = key.toStdString();
    const std::string destStd = destPath.toStdString();
    downloadPool_.start([this, session, refStd, keyStd, destStd, token]() {
        try {
            session->saveAttachment(
                refStd, keyStd, destStd,
                [this, token](std::uint64_t received, std::uint64_t total) {
                    if (!downloadsCancelled_.load()) {
                        emit downloadProgress(
                            token, static_cast<qint64>(received), static_cast<qint64>(total));
                    }
                },
                [this, token](bazarish::client::BlobFetchStage stage) {
                    if (!downloadsCancelled_.load()) {
                        emit downloadStage(token, static_cast<int>(stage));
                    }
                },
                &downloadsCancelled_);
            if (!downloadsCancelled_.load()) {
                emit downloadFinished(token, true, {});
            }
        } catch (const std::exception& e) {
            if (!downloadsCancelled_.load()) {
                emit downloadFinished(token, false, QString::fromUtf8(e.what()));
            }
        }
    });
}

void SessionWorker::exportProfile(const QString& path, const QString& password)
{
    try {
        session_->exportProfile(path.toStdString(), password.toStdString());
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
    connect(this, &SessionController::requestSendText, worker_, &SessionWorker::sendText);
    connect(this, &SessionController::requestSendFile, worker_, &SessionWorker::sendFile);
    connect(this, &SessionController::requestSendReceipt, worker_, &SessionWorker::sendReceipt);
    connect(this, &SessionController::requestSendCallback, worker_, &SessionWorker::sendCallback);
    connect(this, &SessionController::requestSendCommand, worker_, &SessionWorker::sendCommand);
    connect(this, &SessionController::requestSendEdit, worker_, &SessionWorker::sendEdit);
    connect(this, &SessionController::requestSendDelete, worker_, &SessionWorker::sendDelete);
    connect(this, &SessionController::requestSetAvatar, worker_, &SessionWorker::setAvatar);
    connect(this, &SessionController::requestRenameContact, worker_, &SessionWorker::renameContact);
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
    // Download progress / outcome land on the message via the conversation model.
    connect(worker_, &SessionWorker::downloadProgress, this,
        &SessionController::onDownloadProgress);
    connect(worker_, &SessionWorker::downloadStage, this,
        &SessionController::onDownloadStage);
    connect(worker_, &SessionWorker::downloadFinished, this,
        &SessionController::onDownloadFinished);
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
        [this](const QStringList& fps, const QStringList& names) {
            contactFps_ = fps;
            contactNames_.clear();
            for (int i = 0; i < fps.size() && i < names.size(); ++i) {
                if (!names[i].isEmpty()) {
                    contactNames_.insert(fps[i], names[i]);
                }
            }
            rebuildChatList();
            emit activePeerNameChanged();  // the open chat's header may have renamed
        });
    connect(worker_, &SessionWorker::avatarReady, this, &SessionController::onAvatarReady);
    connect(worker_, &SessionWorker::groupsRefreshed, this, &SessionController::onGroupsRefreshed);
    connect(worker_, &SessionWorker::groupCreated, this, &SessionController::onGroupCreated);
    connect(worker_, &SessionWorker::groupMembersReady, this,
        &SessionController::onGroupMembersReady);
    connect(worker_, &SessionWorker::sendProgress, this, &SessionController::onSendProgress);
    connect(worker_, &SessionWorker::uploadProgress, this, &SessionController::onUploadProgress);
    connect(worker_, &SessionWorker::sendResult, this, &SessionController::onSendResult);
    connect(worker_, &SessionWorker::contactRequestSent, this,
        &SessionController::onContactRequestSent);
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
    // There is no persistent outbound queue, so any outgoing message still at
    // "sending" is an interrupted send (the app closed mid-upload), not one in
    // flight. Mark these failed on load so they read as "not sent" with a resend
    // option, instead of a perpetual upload animation.
    store_.failUnsentOnLoad(DeliveryStatus::Sending, DeliveryStatus::Failed);
    emit requestOpen(dir, passphrase);
}

void SessionController::connectServer(const QStringList& facadeUrls, const QString& serverFp)
{
    emit requestConnect(facadeUrls, serverFp, 14);
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

void SessionController::activateConversation(const QString& peer)
{
    activePeer_ = peer;
    emit activePeerChanged();
    emit activePeerNameChanged();
    contacts_.clearUnread(peer);
    // Load the member list for a group conversation (cleared for a 1:1 chat).
    activeGroupMembers_.clear();
    activeGroupAdmin_ = false;
    emit activeGroupChanged();
    if (groupIds_.contains(peer)) {
        emit requestFetchGroupMembers(peer);
    }
}

void SessionController::loadLatestWindow()
{
    // The newest page. A huge conversation opens at its end instantly because only
    // the tail is read; older messages page in when the user scrolls up.
    const QVector<StoredMessage> msgs = store_.latestMessages(activePeer_, kPageSize);
    oldestLoadedId_ = msgs.isEmpty() ? 0 : msgs.front().id;
    newestLoadedId_ = msgs.isEmpty() ? 0 : msgs.back().id;
    hasMoreOlder_ = !msgs.isEmpty() && store_.hasMessagesBefore(activePeer_, oldestLoadedId_);
    hasMoreNewer_ = false;  // the latest page is, by definition, at the newest
    conversation_.setMessages(msgs);
    emit pagingChanged();
}

void SessionController::showInActiveView(const StoredMessage& m, bool isOwn)
{
    if (m.peer != activePeer_) {
        return;  // not the open conversation
    }
    if (hasMoreNewer_) {
        // The window is scrolled back into history (e.g. opened at a search hit),
        // so the newest page is not loaded and a bottom append would be out of
        // place. The message is already persisted. For our own send, jump to the
        // newest page so it is visible; for an incoming one, leave it to the
        // jump-to-latest control.
        if (isOwn) {
            loadLatestWindow();
            emit scrollToBottom();
        }
        return;
    }
    conversation_.appendMessage(m);
    newestLoadedId_ = m.id;
}

void SessionController::openConversation(const QString& peer)
{
    activateConversation(peer);
    loadLatestWindow();
}

void SessionController::saveScroll(const QString& peer, int anchorRow, bool stick)
{
    // The view reports the open conversation's position as the user scrolls, so it
    // is already current the moment this account is switched away.
    scrollPeer_ = peer;
    scrollAnchorRow_ = anchorRow;
    scrollStick_ = stick;
}

QVariantMap SessionController::scrollFor(const QString& peer) const
{
    // "has" is false for any peer we never saved; the view then falls back to
    // pinning to the bottom, the default for a freshly opened conversation.
    QVariantMap m;
    const bool has = !peer.isEmpty() && peer == scrollPeer_;
    m[QStringLiteral("has")] = has;
    m[QStringLiteral("anchor")] = scrollAnchorRow_;
    m[QStringLiteral("stick")] = scrollStick_;
    return m;
}

void SessionController::openConversationAtMessage(const QString& peer, qint64 messageId)
{
    activateConversation(peer);
    // A window ending at the target message (it sits at the window's newest edge),
    // so older context pages in above and newer messages page in below.
    const QVector<StoredMessage> win = store_.olderMessages(peer, messageId + 1, kPageSize);
    oldestLoadedId_ = win.isEmpty() ? 0 : win.front().id;
    newestLoadedId_ = win.isEmpty() ? 0 : win.back().id;
    hasMoreOlder_ = !win.isEmpty() && store_.hasMessagesBefore(peer, oldestLoadedId_);
    hasMoreNewer_ = store_.hasMessagesAfter(peer, newestLoadedId_);
    conversation_.setMessages(win);
    emit pagingChanged();
    emit scrollToMessage(messageId);
}

int SessionController::loadOlderMessages()
{
    if (!hasMoreOlder_ || activePeer_.isEmpty()) {
        return 0;
    }
    const QVector<StoredMessage> older
        = store_.olderMessages(activePeer_, oldestLoadedId_, kPageSize);
    if (older.isEmpty()) {
        hasMoreOlder_ = false;
        emit pagingChanged();
        return 0;
    }
    oldestLoadedId_ = older.front().id;
    hasMoreOlder_ = store_.hasMessagesBefore(activePeer_, oldestLoadedId_);
    conversation_.prependMessages(older);
    emit pagingChanged();
    return static_cast<int>(older.size());
}

int SessionController::loadNewerMessages()
{
    if (!hasMoreNewer_ || activePeer_.isEmpty()) {
        return 0;
    }
    const QVector<StoredMessage> newer
        = store_.newerMessages(activePeer_, newestLoadedId_, kPageSize);
    if (newer.isEmpty()) {
        hasMoreNewer_ = false;
        emit pagingChanged();
        return 0;
    }
    newestLoadedId_ = newer.back().id;
    hasMoreNewer_ = store_.hasMessagesAfter(activePeer_, newestLoadedId_);
    conversation_.appendMessages(newer);
    emit pagingChanged();
    return static_cast<int>(newer.size());
}

void SessionController::jumpToLatest()
{
    if (activePeer_.isEmpty()) {
        return;
    }
    if (hasMoreNewer_) {
        loadLatestWindow();  // a model reset; the view autoscrolls to the bottom
    }
    emit scrollToBottom();
}

bool SessionController::atNewest() const
{
    return !hasMoreNewer_;
}

bool SessionController::hasMoreOlder() const
{
    return hasMoreOlder_;
}

QVariantList SessionController::searchMessages(const QString& query)
{
    QVariantList results;
    if (activePeer_.isEmpty()) {
        return results;
    }
    const bool isGroup = groupIds_.contains(activePeer_);
    for (const SearchHit& hit : store_.searchInPeer(activePeer_, query)) {
        QVariantMap row;
        row["id"] = hit.id;
        row["text"] = hit.text;
        row["time"] = hit.ts;
        row["outgoing"] = hit.outgoing;
        // For a group hit, label the author; a 1:1 hit is "you" or the peer.
        if (isGroup && !hit.sender.isEmpty()) {
            row["author"] = shortFingerprint(hit.sender);
        } else {
            row["author"] = hit.outgoing ? QStringLiteral("You") : peerName(activePeer_);
        }
        results.push_back(row);
    }
    return results;
}

void SessionController::rebuildChatList()
{
    QVector<ContactRow> rows;
    for (const QString& fp : contactFps_) {
        rows.push_back(
            ContactRow{fp, peerName(fp), store_.lastText(fp), store_.lastTime(fp), 0, false});
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
    const QString name = contactNames_.value(id);
    if (!name.isEmpty()) {
        return name;  // the local display name (alias / invite name / rename)
    }
    return shortFingerprint(id);
}

QString SessionController::contactName(const QString& fp) const
{
    return contactNames_.value(fp);
}

void SessionController::setAvatar(const QString& fileUrl)
{
    const QString localPath = QUrl(fileUrl).toLocalFile();
    if (!localPath.isEmpty()) {
        emit requestSetAvatar(localPath);
    }
}

void SessionController::renameContact(const QString& fp, const QString& name)
{
    if (fp.isEmpty()) {
        return;
    }
    const QString trimmed = name.trimmed();
    // Optimistic local update so the UI reflects the rename at once; the worker
    // persists it and mirrors it to the account's own other devices.
    if (trimmed.isEmpty()) {
        contactNames_.remove(fp);
    } else {
        contactNames_.insert(fp, trimmed);
    }
    rebuildChatList();
    emit activePeerNameChanged();
    emit requestRenameContact(fp, trimmed);
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
        gm.ts = nowMillis();
        gm.orderKey = gm.ts;
        gm.status = DeliveryStatus::Sending;
        gm.id = store_.append(gm);
        statusById_[gm.id] = DeliveryStatus::Sending;
        showInActiveView(gm, true);
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
    m.ts = nowMillis();
    m.orderKey = m.ts;
    m.status = DeliveryStatus::Sending;
    m.id = store_.append(m);
    statusById_[m.id] = DeliveryStatus::Sending;
    showInActiveView(m, true);
    contacts_.touch(activePeer_, {}, text, m.ts, false);
    emit requestSendText(activePeer_, text, m.id, m.protocolId);
}

void SessionController::sendFile(const QString& fileUrl, qint64 ttlSeconds, int downloadCount)
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
    // Record the local size and mime so the sender's own bubble renders a real
    // attachment card (name + size) immediately, without waiting for the upload.
    const QFileInfo info(localPath);
    m.attSize = info.size();
    m.attMime = QMimeDatabase().mimeTypeForFile(info).name();
    // Keep the local source path so a failed send can be resent without re-picking
    // the file (the bytes are not kept; only the path).
    m.attSrcPath = localPath;
    m.ts = nowMillis();
    m.orderKey = m.ts;
    m.status = 0;
    m.id = store_.append(m);
    statusById_[m.id] = 0;
    fileRetention_.insert(m.id, FileRetention{ttlSeconds, downloadCount});
    showInActiveView(m, true);
    contacts_.touch(activePeer_, {}, "[file] " + m.attName, m.ts, false);
    emit requestSendFile(activePeer_, localPath, m.id, m.protocolId, ttlSeconds, downloadCount);
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
        contacts_.touch(activePeer_, {}, trimmed, nowMillis(), false);
        // The edited version starts its delivery afresh: reset the bubble's status
        // to "sending" (grey) and clear any prior error, so it then advances on the
        // edit's own delivery instead of showing the original message's state.
        statusById_[editingLocalId_] = DeliveryStatus::Sending;
        store_.updateStatus(editingLocalId_, DeliveryStatus::Sending);
        conversation_.setStatusForId(editingLocalId_, DeliveryStatus::Sending);
        conversation_.setErrorForId(editingLocalId_, {});
        emit requestSendEdit(activePeer_, editingProtocolId_, editingLocalId_, trimmed);
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

void SessionController::deleteMessage(qint64 localId, const QString& protocolId, bool outgoing)
{
    if (activePeer_.isEmpty() || localId == 0) {
        return;
    }
    const bool isGroup = groupIds_.contains(activePeer_);
    // Remove our own copy with no trace.
    store_.removeById(localId);
    conversation_.removeById(localId);
    statusById_.remove(localId);
    // Refresh the chat-list preview to whatever the new last message now is.
    contacts_.touch(activePeer_, isGroup ? groupNames_.value(activePeer_) : QString(),
        store_.lastText(activePeer_), store_.lastTime(activePeer_), false, isGroup);
    // Ask the recipient to delete it too, but only for our own one-to-one message:
    // a peer cannot be told to drop a message we received from them, and a group
    // fan-out delete is out of scope - those stay local-only.
    if (outgoing && !isGroup && !protocolId.isEmpty()) {
        emit requestSendDelete(activePeer_, protocolId);
    }
}

void SessionController::copyText(const QString& text) const
{
    if (QClipboard* const clipboard = QGuiApplication::clipboard()) {
        clipboard->setText(text);
    }
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
        emit requestSaveAttachment(ref, key, localPath, 0);
    }
}

void SessionController::saveAttachmentToFile(const QString& ref, const QString& key,
    const QString& fileUrl, qint64 token)
{
    const QString dest = QUrl(fileUrl).toLocalFile();
    if (dest.isEmpty()) {
        conversation_.finishDownloadForId(
            token, false, QStringLiteral("Choose where to save the file."));
        return;
    }
    // Mark the message as downloading at once, so the bubble shows activity even
    // before the first byte-progress callback arrives.
    conversation_.setDownloadProgressForId(token, 0, 0);
    // Remember the destination so a successful download can record where it landed
    // (for the later "Open" action).
    pendingSavePath_.insert(token, dest);
    emit requestSaveAttachment(ref, key, dest, token);
}

QUrl SessionController::defaultSaveUrl(const QString& fileName) const
{
    QString dir = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    if (dir.isEmpty()) {
        dir = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
    }
    const QString name = fileName.isEmpty() ? QStringLiteral("file") : fileName;
    return QUrl::fromLocalFile(QDir(dir).filePath(name));
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

    // A read receipt: the peer read our referenced message (the green state), and
    // by the read high-water everything we sent them before it too. Not shown.
    if (type == "receipt") {
        const qint64 localId = store_.idForProtocol(message.value("ref").toString());
        if (localId != 0) {
            markOutgoingRead(peer, localId);
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
            // If we had already read this message, the edit is read again the
            // moment it lands in the open chat: re-acknowledge it so the sender's
            // edited bubble can advance to delivered (green), the same way a fresh
            // message does. (ref is the protocol id our copy is stored under.)
            if (peer == activePeer_ && sendReceipts_
                && localId <= lastReadAckedId_.value(peer, 0)) {
                emit requestSendReceipt(peer, message.value("ref").toString());
            }
            QString preview = newText;
            if (preview.isEmpty() && !newKeyboard.isEmpty()) {
                preview = "[interactive]";
            }
            contacts_.touch(peer, peerName(peer), preview, nowMillis(), peer != activePeer_);
        }
        return;
    }

    // A delete-for-everyone of a message this peer previously sent us: remove it
    // with no trace. Scoped to incoming-from-peer in the store, so a peer can only
    // delete its own messages.
    if (type == "delete") {
        const qint64 localId
            = store_.idForIncomingProtocol(message.value("ref").toString(), peer);
        if (localId != 0) {
            store_.removeById(localId);
            if (peer == activePeer_) {
                conversation_.removeById(localId);
            }
            contacts_.touch(peer, peerName(peer), store_.lastText(peer), store_.lastTime(peer), false);
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
        sys.ts = nowMillis();
        sys.orderKey = sys.ts;
        sys.status = DeliveryStatus::Received;
        sys.id = store_.append(sys);
        showInActiveView(sys, false);
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

    // Idempotent receive. The mailbox is at-least-once: a blob whose ack was lost
    // (or that we processed just before a restart) is legitimately re-offered and
    // arrives here again with the same id. Dedup against the transcript we already
    // keep - if this conversation already holds an incoming message with this id,
    // this is that redelivery: never store or surface it a second time. (A read
    // receipt is only sent on a real read, handled by markReadThroughRow.)
    const QString incomingId = message.value("messageId").toString();
    if (!incomingId.isEmpty() && store_.idForIncomingProtocol(incomingId, convKey) != 0) {
        return;
    }

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
    // Order by and display the sender's own sentAt (ms): a recent burst that
    // arrived out of order is reordered into place; a long-delayed arrival is
    // appended at the end as new (docs-main Messages.md "Ordering and timestamps").
    const Placement placement = placeReceived(message.value("sentAt").toLongLong(), nowMillis());
    m.ts = placement.displayTs;
    m.orderKey = placement.orderKey;
    m.status = DeliveryStatus::Received;  // incoming; no indicator rendered
    m.id = store_.append(m);

    showInActiveView(m, false);
    QString preview = m.text;
    if (preview.isEmpty() && !m.attName.isEmpty()) {
        preview = "[" + type + "] " + m.attName;
    }
    if (isGroupMsg) {
        contacts_.touch(convKey, groupNames_.value(convKey), preview, m.ts,
            convKey != activePeer_, true);
    } else {
        contacts_.touch(peer, peerName(peer), preview, m.ts, peer != activePeer_);
    }
    // No receipt is sent on arrival: the green "read" state is reported only when
    // the user actually reads the message (chat open + window focused + the message
    // in view), driven by markReadThroughRow.
}

void SessionController::onAvatarReady(const QString& fingerprint, const QByteArray& data)
{
    AvatarStore::instance().put(fingerprint, data);
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

void SessionController::onUploadProgress(qint64 localId, qint64 sent, qint64 total)
{
    const double fraction = total > 0 ? static_cast<double>(sent) / static_cast<double>(total) : 0.0;
    conversation_.setUploadProgressForId(localId, fraction);
}

void SessionController::onDownloadProgress(qint64 token, qint64 received, qint64 total)
{
    conversation_.setDownloadProgressForId(token, received, total);
}

void SessionController::onDownloadStage(qint64 token, int stage)
{
    conversation_.setDownloadStageForId(token, stage);
}

void SessionController::onDownloadFinished(qint64 token, bool ok, const QString& error)
{
    const QString path = pendingSavePath_.take(token);
    // A 404/410 means the blob has aged out of the store (its TTL or download
    // count is spent) and will never come back. Record that permanently so the
    // bubble shows "Not found" with no Save button, even after a restart, instead
    // of a transient retryable error.
    const bool notFound = !ok
        && (error.contains(QStringLiteral("status 404")) || error.contains(QStringLiteral("status 410")));
    if (notFound) {
        store_.setBlobGone(token, true);
        conversation_.setBlobGoneForId(token, true);
        conversation_.finishDownloadForId(token, false, QString());
        return;
    }
    conversation_.finishDownloadForId(token, ok, error);
    if (ok && !path.isEmpty()) {
        // Remember where it landed, in the store and the open view, so the bubble
        // can offer to open it (falling back to re-save when the file is gone).
        store_.setSavedPath(token, path);
        conversation_.setSavedPathForId(token, path);
    }
}

bool SessionController::fileExists(const QString& path) const
{
    return !path.isEmpty() && QFileInfo::exists(path);
}

void SessionController::showInFolder(const QString& path) const
{
    if (path.isEmpty()) {
        return;
    }
    const QFileInfo info(path);
#if defined(Q_OS_LINUX) && defined(BAZARISH_HAVE_QTDBUS)
    // Ask the desktop's file manager to reveal the file with it selected, via the
    // freedesktop.org FileManager1 D-Bus interface (Nautilus, Dolphin, Nemo, ...).
    if (info.exists()) {
        QDBusInterface fm(QStringLiteral("org.freedesktop.FileManager1"),
            QStringLiteral("/org/freedesktop/FileManager1"),
            QStringLiteral("org.freedesktop.FileManager1"), QDBusConnection::sessionBus());
        if (fm.isValid()) {
            const QStringList uris{QUrl::fromLocalFile(info.absoluteFilePath()).toString()};
            const QDBusReply<void> reply = fm.call(QStringLiteral("ShowItems"), uris, QString());
            if (reply.isValid()) {
                return;
            }
        }
    }
#endif
    // Fallback (no D-Bus file manager, or the call failed): open the containing
    // directory without a selection.
    const QString dir = info.absolutePath();
    if (!dir.isEmpty()) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
    }
}

void SessionController::onSendResult(qint64 localId, bool ok, const QString& error)
{
    if (ok) {
        // The grey state (and yellow, when the server confirmed the recipient
        // stored it) were already set via sendProgress; a still-pending delivery
        // stays grey on purpose. Just clear any prior failure note.
        conversation_.setErrorForId(localId, {});
        return;
    }
    // A delivery failure belongs to one message, not the whole app: mark that
    // bubble failed and attach the reason inline (with a resend affordance in the
    // UI) instead of raising an application-wide error banner.
    bumpStatus(localId, DeliveryStatus::Failed);
    conversation_.setErrorForId(localId, error);
}

void SessionController::resendText(qint64 localId, const QString& text, const QString& protocolId)
{
    if (activePeer_.isEmpty() || text.isEmpty()) {
        return;
    }
    // Reset to "sending" and clear the prior error, then re-dispatch with the
    // SAME protocol id so the recipient's server still deduplicates it (a retry
    // must never double-deliver).
    statusById_[localId] = DeliveryStatus::Sending;
    store_.updateStatus(localId, DeliveryStatus::Sending);
    conversation_.setStatusForId(localId, DeliveryStatus::Sending);
    conversation_.setErrorForId(localId, {});
    emit requestSendText(activePeer_, text, localId, protocolId);
}

void SessionController::resendFile(qint64 localId, const QString& protocolId)
{
    if (activePeer_.isEmpty()) {
        return;
    }
    const QString srcPath = store_.sourcePathFor(localId);
    if (srcPath.isEmpty() || !QFileInfo::exists(srcPath)) {
        // The original file is no longer on disk (or predates path recording): let
        // the UI pick a file to send. The failed bubble stays as a record.
        emit resendFilePickRequested();
        return;
    }
    // Reset to "sending" and re-upload from the saved path, reusing this bubble.
    // Same protocol id as resendText: the inner content id is preserved so the
    // recipient still recognises the message.
    statusById_[localId] = DeliveryStatus::Sending;
    store_.updateStatus(localId, DeliveryStatus::Sending);
    conversation_.setStatusForId(localId, DeliveryStatus::Sending);
    conversation_.setErrorForId(localId, {});
    const FileRetention r = fileRetention_.value(localId);
    emit requestSendFile(activePeer_, srcPath, localId, protocolId, r.ttlSeconds, r.downloadCount);
}

void SessionController::markOutgoingRead(const QString& peer, qint64 uptoId)
{
    // Persist the green high-water (covers paged-out rows too)...
    store_.markOutgoingReadUpTo(peer, uptoId, DeliveryStatus::Delivered,
        DeliveryStatus::AtSenderServer, DeliveryStatus::AtRecipientServer);
    // ...and reflect it in the open window.
    if (peer == activePeer_) {
        for (const qint64 id : conversation_.markDeliveredThrough(uptoId)) {
            statusById_[id] = DeliveryStatus::Delivered;
        }
    }
}

void SessionController::markReadThroughRow(int row)
{
    // The user actually read up to `row` (the view is open, focused and scrolled
    // to it): send a read receipt for the newest incoming message at or before it,
    // advancing a per-peer high-water so we send at most one receipt per new read.
    if (activePeer_.isEmpty() || row < 0) {
        return;
    }
    qint64 id = 0;
    QString protocolId;
    if (!conversation_.newestIncomingThrough(row, id, protocolId)) {
        return;
    }
    if (id <= lastReadAckedId_.value(activePeer_, 0)) {
        return;  // already acknowledged up to here
    }
    lastReadAckedId_[activePeer_] = id;
    emit requestSendReceipt(activePeer_, protocolId);
}

void SessionController::onContactRequestSent(const QString& fingerprint, const QString& intro)
{
    // Mirror the request on our own side: store the intro we just sent as an
    // outgoing message and open a chat for the new peer, so adding a contact
    // produces a visible conversation immediately instead of an empty roster
    // entry. The contact itself is already persisted by the core session; the
    // following sync() refresh will keep the chat list consistent.
    if (fingerprint.isEmpty()) {
        return;
    }
    const QString body = intro.isEmpty() ? QStringLiteral("Contact request sent.") : intro;
    StoredMessage m;
    m.peer = fingerprint;
    m.outgoing = true;
    m.type = "contact.request";
    m.protocolId = SessionController_genProtocolId();
    m.text = body;
    m.ts = nowMillis();
    m.orderKey = m.ts;
    // The request was delivered to the peer's server before this fires (the add
    // call returned without throwing), so it is honestly past our own server.
    m.status = DeliveryStatus::AtRecipientServer;
    m.id = store_.append(m);
    statusById_[m.id] = m.status;
    showInActiveView(m, true);
    contacts_.touch(fingerprint, {}, body, m.ts, false);
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
