// Bazarish project (c) 2026
#include "SessionWorker.hpp"

#include "SessionShared.hpp"

#include "QtAudioIo.hpp"
#include "I2pRouter.hpp"
#include "FederationFetch.hpp"

#include "DeliveryStatus.hpp"
#include "DevicePairing.hpp"
#include "Session.hpp"

#include <bazarish/Crypto.hpp>
#include <bazarish/Limits.hpp>
#include <bazarish/Descriptor.hpp>
#include <bazarish/Portal.hpp>

#pragma push_macro("emit")
#undef emit
#include <bazarish/Log.hpp>
#pragma pop_macro("emit")

#include <QBuffer>
#include <QByteArray>
#include <QDateTime>
#include <QImage>
#include <chrono>
#include <QTimer>
#include <cstring>

#if defined(Q_OS_LINUX) && defined(BAZARISH_HAVE_QTDBUS)
#endif

#include <algorithm>
#include <array>
#include <ctime>
#include <exception>
#include <fstream>
#include <iterator>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

namespace bazarish::app {

struct ResolvedContactAddQueue {
    std::mutex mutex;
    struct Entry {
        QString opId;
        bazarish::client::Session::ContactCardResolved resolved;
    };
    std::vector<Entry> results;
    std::vector<std::pair<QString, QString>> stages;
};

struct AliasErrandQueue {
    std::mutex mutex;
    std::vector<bazarish::client::Session::AliasErrandResult> results;
};

using bazarish::client::IncomingMessage;
using bazarish::client::ServerEndpoint;
using bazarish::client::Session;

const char* const kCoreProgress[] = {
    QT_TR_NOOP("Starting the I2P router"),
    QT_TR_NOOP("Starting with the built-in reseeds"),
    QT_TR_NOOP("Building your I2P tunnels"),
    QT_TR_NOOP("I2P tunnels are still building"),
    QT_TR_NOOP("Looking up the server's I2P address"),
    QT_TR_NOOP("Connected to the server over I2P"),
    QT_TR_NOOP("Registering with this server"),
    QT_TR_NOOP("Registered; registering this device"),
    QT_TR_NOOP("Checking the address your server serves"),
    QT_TR_NOOP("Publishing your own destination"),
    QT_TR_NOOP("Delegating your destination to the server"),
    QT_TR_NOOP("Publishing your contact card"),
    QT_TR_NOOP("Syncing your address to your other devices"),
    QT_TR_NOOP("Telling the name service where you are"),
    QT_TR_NOOP("Asking your server for a new serving key"),
    QT_TR_NOOP("Signing a card over the new key"),
    QT_TR_NOOP("Putting the new key in force"),
    QT_TR_NOOP("Telling the name service"),
    QT_TR_NOOP("Telling your contacts"),
};

namespace {
constexpr std::size_t kContactRequestIdBytes = 8;

class WorkerOp {
public:
    WorkerOp(SessionWorker* const worker, const QString& id, const QString& kind,
        const QString& title, const QString& status)
        : worker_(worker)
        , id_(id)
    {
        emit worker_->opBegin(id_, kind, title, status);
    }
    WorkerOp(const WorkerOp&) = delete;
    WorkerOp& operator=(const WorkerOp&) = delete;
    ~WorkerOp() { emit worker_->opDone(id_, ok_, status_); }

    void succeed(const QString& status)
    {
        ok_ = true;
        status_ = status;
    }
    void fail(const QString& status)
    {
        ok_ = false;
        status_ = status;
    }

private:
    SessionWorker* const worker_;
    const QString id_;
    bool ok_ = false;
    QString status_ = SessionWorker::tr("Failed");
};

constexpr std::chrono::milliseconds kSlowPass{400};

constexpr std::chrono::milliseconds kSlowStretch{200};

constexpr qint64 kTransientCheckIntervalMs = 3600 * 1000;

constexpr qint64 kApprovalCheckIntervalMs = 60 * 1000;

constexpr qint64 kRegisterRetryIntervalMs = 60 * 1000;

constexpr qint64 kAliasServiceIntervalMs = 60 * 1000;

constexpr qint64 kTransientRenewLeadSeconds = 5 * 24 * 3600;

constexpr qint64 kTransientJitterSeconds = 6 * 3600;

constexpr int kCallWatchIntervalMs = 100;

constexpr int kEventWaitSeconds = 30;

constexpr int kMaintenanceIntervalMs = 2000;

QVariantList aliasHoldingRows(const std::vector<bazarish::client::Session::AliasHolding>& held)
{
    QVariantList rows;
    for (const bazarish::client::Session::AliasHolding& holding : held) {
        if (!holding.bindingWanted) {
            continue;
        }
        QVariantMap row;
        row[QStringLiteral("alias")] = QString::fromStdString(holding.alias);
        const QString when = QDateTime::fromSecsSinceEpoch(holding.notAfter).date().toString(
            QStringLiteral("yyyy-MM-dd"));
        row[QStringLiteral("term")] = holding.autoRenew
            ? SessionWorker::tr("renews %1").arg(when)
            : SessionWorker::tr("expires %1").arg(when);
        rows << row;
    }
    return rows;
}

QString aliasHoldingsNote(const QVariantList& rows, const bool depositCovers)
{
    if (rows.isEmpty() || depositCovers) {
        return QString();
    }
    return SessionWorker::tr("Your deposit will not cover the next renewal.");
}

bazarish::client::DeliveryWatch watchFor(SessionWorker* const worker, const qint64 localId)
{
    bazarish::client::DeliveryWatch watch;
    watch.onPhase = [worker, localId](const std::string& phase) {
        if (phase == bazarish::client::kPhaseDialing) {
            emit worker->sendProgress(localId, DeliveryStatus::Delivering);
        }
        emit worker->sendPhase(localId, QString::fromStdString(phase));
    };
    watch.onOutcome
        = [worker, localId](const bazarish::client::OutboundCourier::Outcome& outcome) {
              QMetaObject::invokeMethod(worker, "refreshContacts", Qt::QueuedConnection);
              if (outcome.stored) {
                  emit worker->sendProgress(localId, DeliveryStatus::AtRecipientServer);
                  emit worker->sendResult(localId, true, {});
                  return;
              }
              emit worker->sendResult(localId, false,
                  outcome.errorMessage.empty()
                      ? SessionWorker::tr("the recipient's server could not be reached")
                      : QString::fromStdString(outcome.errorMessage));
          };
    return watch;
}

QByteArray compressAvatarJpeg(const QImage& img)
{
    if (img.isNull()) {
        return {};
    }
    const int side = std::min(img.width(), img.height());
    QImage square = img.copy((img.width() - side) / 2, (img.height() - side) / 2, side, side);
    constexpr int kDim = 256;
    if (square.width() > kDim) {
        square = square.scaled(kDim, kDim, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    }
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
    return bytes;
}

}  // namespace

SessionWorker::~SessionWorker()
{
    downloadsCancelled_.store(true);
    downloadPool_.waitForDone();
    stopEventWaiter();
    stopErrands();
}

void SessionWorker::startReceiving()
{
    if (maintenanceTimer_ == nullptr) {
        maintenanceTimer_ = new QTimer(this);
        maintenanceTimer_->setInterval(kMaintenanceIntervalMs);
        connect(maintenanceTimer_, &QTimer::timeout, this, &SessionWorker::maintain);
    }
    if (!maintenanceTimer_->isActive()) {
        maintenanceTimer_->start();
    }
    startEventWaiter();
}

void SessionWorker::startErrands()
{
    if (errands_.joinable()) {
        return;
    }
    errandsRunning_ = true;
    errands_ = std::thread([this]() {
        while (true) {
            Errand errand;
            {
                std::unique_lock<std::mutex> lock(errandMutex_);
                errandWake_.wait(
                    lock, [this]() { return !errandsRunning_ || !errandQueue_.empty(); });
                if (!errandsRunning_ && errandQueue_.empty()) {
                    return;
                }
                errand = std::move(errandQueue_.front());
                errandQueue_.pop_front();
            }
            if (!errand.deliveryId.empty()) {
                try {
                    session_->submitPrepared(errand.deliveryId, errand.sealed, errand.kind);
                } catch (const std::exception& error) {
                    bazarish::log::warn(
                        "could not echo what was sent to our own devices: {}", error.what());
                }
                continue;
            }
            try {
                session_->releasePending(errand.pendingId);
            } catch (const std::exception& error) {
                bazarish::log::warn("pending item not given back: {}", error.what());
            }
            QMetaObject::invokeMethod(
                this,
                [this, pendingId = errand.pendingId]() {
                    if (session_) {
                        session_->forgetPending(pendingId);
                    }
                    settleDrain();
                },
                Qt::QueuedConnection);
        }
    });
}

void SessionWorker::stopErrands()
{
    if (!errands_.joinable()) {
        return;
    }
    {
        const std::lock_guard<std::mutex> lock(errandMutex_);
        errandsRunning_ = false;
        errandQueue_.clear();
    }
    errandWake_.notify_all();
    errands_.join();
}

void SessionWorker::startEventWaiter()
{
    if (eventWaiter_.joinable() || !session_) {
        return;
    }
    bazarish::client::Session::ContactFetchContext context;
    try {
        context = session_->contactFetchContext();
    } catch (const std::exception& error) {
        bazarish::log::warn("no event waiter: {}", error.what());
        return;
    }
    eventWaiterRunning_ = std::make_shared<std::atomic<bool>>(true);
    eventWaiter_ = std::thread([this, context, running = eventWaiterRunning_]() {
        std::unique_ptr<bazarish::client::Client> waiter;
        while (running->load()) {
            std::vector<bazarish::client::PendingEntry> waiting;
            try {
                if (!waiter) {
                    waiter = bazarish::client::Session::makeEventClient(context);
                }
                waiting = bazarish::client::Session::waitForMail(*waiter, kEventWaitSeconds);
            } catch (const std::exception& error) {
                const QString reason = QString::fromUtf8(error.what());
                QMetaObject::invokeMethod(
                    this, [this, reason]() { emit syncReachable(false, reason); },
                    Qt::QueuedConnection);
                bazarish::log::info("mail wait failed, asking again: {}", error.what());
                continue;
            }
            if (!running->load()) {
                return;
            }
            QMetaObject::invokeMethod(
                this, [this]() { emit syncReachable(true, {}); }, Qt::QueuedConnection);
            if (waiting.empty()) {
                continue;
            }
            try {
                std::vector<bazarish::client::Session::MailboxItem> ahead
                    = bazarish::client::Session::fetchMailbox(
                        *waiter, waiting, bazarish::client::Session::kPendingItemsPerPass);
                if (!ahead.empty()) {
                    QMetaObject::invokeMethod(
                        this,
                        [this, items = std::move(ahead)]() mutable {
                            if (session_) {
                                session_->holdFetched(std::move(items));
                            }
                        },
                        Qt::QueuedConnection);
                }
            } catch (const std::exception& error) {
                bazarish::log::info("nothing fetched ahead of the pass: {}", error.what());
            }
            {
                const std::lock_guard<std::mutex> lock(drainMutex_);
                drainSettled_ = false;
            }
            QMetaObject::invokeMethod(this, "sync", Qt::QueuedConnection);
            std::unique_lock<std::mutex> lock(drainMutex_);
            drainDone_.wait(
                lock, [this, &running]() { return drainSettled_ || !running->load(); });
        }
    });
}

void SessionWorker::stopEventWaiter()
{
    if (eventWaiterRunning_) {
        eventWaiterRunning_->store(false);
    }
    drainDone_.notify_all();
    if (eventWaiter_.joinable()) {
        eventWaiter_.join();
    }
    eventWaiter_ = std::thread();
    eventWaiterRunning_.reset();
}

void SessionWorker::settleDrain()
{
    if (session_ != nullptr && !session_->morePending() && session_->awaitingAcks() > 0) {
        return;
    }
    {
        const std::lock_guard<std::mutex> lock(drainMutex_);
        drainSettled_ = true;
    }
    drainDone_.notify_all();
}

void SessionWorker::openAccount(
    const QString& dir, const QString& passphrase, const bool startOnline)
{
    downloadPool_.setMaxThreadCount(3);
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
    // Real microphone/speaker for calls (Qt Multimedia).
    session_->setAudioBackend(
        []() -> std::unique_ptr<bazarish::AudioSource> { return std::make_unique<QtAudioSource>(); },
        []() -> std::unique_ptr<bazarish::AudioSink> { return std::make_unique<QtAudioSink>(); });
    session_->onServerAnswered([this]() { emit syncReachable(true, {}); });
    session_->setAckSink([this](const std::string& pendingId) { queueAck(pendingId); });
    session_->setSelfSendSink(
        [this](std::string deliveryId, bazarish::Bytes sealed, std::string kind) {
            Errand errand;
            errand.deliveryId = std::move(deliveryId);
            errand.sealed = std::move(sealed);
            errand.kind = std::move(kind);
            queueErrand(std::move(errand));
        });
    emit loginSignerReady(session_->loginSigner());
    session_->onAddressDecision([this](const std::string& served, const std::string& ours) {
        emit addressMismatch(QString::fromStdString(served), QString::fromStdString(ours));
    });
    const bool connected = session_->isConnected();
    emit opened(QString::fromStdString(session_->fingerprint()),
        QString::fromStdString(session_->displayName()), connected);
    emit accountSettings(session_->acceptCalls(), session_->sendReceipts(),
        session_->sharingAllowed());
    {
        const QVariantList rows = aliasHoldingRows(session_->aliasNames());
        if (!rows.isEmpty()) {
            emit aliasHoldings(rows, aliasHoldingsNote(rows, session_->aliasDepositCovers()));
        }
    }
    emitContacts();
    if (startOnline) {
        resumePendingAdds();
    }
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
    emitFacadeInfo();
    session_->setTransferHandler([this](const bazarish::client::TransferEvent& event) {
        const QString id = QString::fromStdString(event.e2eId);
        const QString peer = QString::fromStdString(event.peer);
        if (!event.stage.empty()) {
            emit transferStage(peer, id, QString::fromStdString(event.stage));
        }
        switch (event.state) {
        case bazarish::client::TransferState::eRunning:
            emit servedProgress(peer, id, static_cast<qint64>(event.bytes),
                static_cast<qint64>(event.total));
            break;
        case bazarish::client::TransferState::eDone:
            emit servedFinished(peer, id, true, {});
            break;
        case bazarish::client::TransferState::eFailed:
            emit servedFinished(peer, id, false, QString::fromStdString(event.error));
            break;
        case bazarish::client::TransferState::eRequested:
            break;
        }
    });
    try {
        emit inviteReady(QString::fromStdString(session_->inviteUri()));
    } catch (const std::exception& error) {
        bazarish::log::info("no invite yet: {}", error.what());
    }
    emit i2pKeyState(session_->hasI2pDestination(),
        session_->hasI2pDestination()
            ? QString::fromStdString(session_->i2pAddress())
            : QString());
    session_->setSwitchedOff(!startOnline);
    if (connected && startOnline) {
        startReceiving();
        sync();
    }
}

void SessionWorker::emitContacts()
{
    if (!session_) {
        return;
    }
    QVector<ContactState> contacts;
    for (const std::string& fp : session_->contactFingerprints()) {
        ContactState contact;
        contact.fingerprint = QString::fromStdString(fp);
        contact.name = QString::fromStdString(session_->contactDisplayName(fp));
        if (!session_->contactIsPending(fp)) {
            contact.request = ContactState::eAnswered;
        } else if (session_->contactAcceptInFlight(fp)) {
            contact.request = ContactState::eAccepting;
        } else {
            contact.request = ContactState::eWaiting;
        }
        contact.sharingRefused = session_->contactSharingRefused(fp);
        try {
            contact.invite = QString::fromStdString(session_->contactInviteUri(fp));
        } catch (const std::exception& error) {
            bazarish::log::debug("contact {} is not shareable yet: {}",
                bazarish::log::redact(fp), error.what());
        }
        contact.writable = session_->canWriteTo(fp);
        contact.notifications = session_->contactNotifications(fp);
        contact.calls = session_->contactCalls(fp);
        contacts.push_back(contact);
    }
    QStringList blocked;
    for (const std::string& fp : session_->blockedPeers()) {
        blocked << QString::fromStdString(fp);
    }
    emit contactsRefreshed(contacts, blocked);
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
    QStringList reseeds;
    for (const std::string& url : session_->endpoint().reseeds) {
        reseeds << QString::fromStdString(url);
    }
    emit facadeInfo(QString::fromStdString(session_->activeFacadeUrl()), configured,
        QString::fromStdString(session_->endpoint().serverFingerprint), reseeds);
}

void SessionWorker::connectAndRegister(const QStringList& facadeUrls, const QString& serverFp,
    const QStringList& reseedUrls)
{
    if (!session_) {
        emit actionFailed(tr("no account is open"));
        return;
    }
    try {
        emit connectProgress(5, tr("Preparing"));
        bazarish::client::setConnectProgressSink(
            [this](const int percent, const std::string& text) {
                emit connectProgress(percent, coreText(QString::fromStdString(text)));
            });
        bazarish::client::setBootstrapNoticeSink([this](const std::string& text) {
            emit actionFailed(QString::fromStdString(text));
        });
        ServerEndpoint endpoint;
        endpoint.serverFingerprint = serverFp.toStdString();
        for (const QString& url : facadeUrls) {
            const QString trimmed = url.trimmed();
            if (!trimmed.isEmpty()) {
                endpoint.facades.push_back(
                    bazarish::client::parseFacadeUrl(trimmed.toStdString()));
            }
        }
        for (const QString& url : reseedUrls) {
            const QString trimmed = url.trimmed();
            if (trimmed.isEmpty()) {
                continue;
            }
            if (!trimmed.startsWith(QStringLiteral("https://"))) {
                throw std::runtime_error(
                    tr("A reseed address must be an https URL: %1").arg(trimmed).toStdString());
            }
            endpoint.reseeds.push_back(trimmed.toStdString());
        }
        if (endpoint.facades.empty()) {
            throw std::runtime_error(tr("Enter at least one facade URL").toStdString());
        }
        session_->connectServer(endpoint);
        emitFacadeInfo();
        // Registering publishes this account's card, which a switched-off account must not do.
        if (session_->switchedOff()) {
            bazarish::client::setConnectProgressSink({});
            emit actionFailed(tr("this account is switched off: the server connection is saved, "
                                 "switch the account on to connect"));
            return;
        }
        const bool overI2p = std::any_of(endpoint.facades.begin(), endpoint.facades.end(),
            [](const bazarish::client::Facade& f) {
                return f.host.size() > 8 && f.host.rfind(".b32.i2p") == f.host.size() - 8;
            });
        emit connectProgress(overI2p ? 8 : 20,
            overI2p ? tr("Connecting over I2P — the first connection takes minutes")
                    : tr("Connecting to the server"));
        session_->registerAccount();
    } catch (const std::exception& e) {
        bazarish::client::setConnectProgressSink({});
        const QString reason = QString::fromUtf8(e.what());
        emit connectionChanged(false, reason);
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
        } catch (const std::exception& error) {
            bazarish::log::warn("portal info unavailable: {}", error.what());
        }
        emit actionFailed(reason);
        return;
    }
    emit connectProgress(100, tr("Connected"));
    bazarish::client::setConnectProgressSink({});
    emit connectionChanged(true, "active");
    emit actionOk(tr("Connected"));
    emitFacadeInfo();
    startReceiving();
    sync();
}

void SessionWorker::setSyncEnabled(bool on)
{
    if (session_) {
        session_->setSwitchedOff(!on);
    }
    if (on) {
        startReceiving();
        sync();
    } else {
        stopEventWaiter();
        if (maintenanceTimer_ != nullptr) {
            maintenanceTimer_->stop();
        }
        if (session_) {
            session_->releaseI2pLinks();
        }
    }
}

void SessionWorker::rebuildI2pLinks()
{
    stopEventWaiter();
    if (session_) {
        session_->releaseI2pLinks();
    }
    if (maintenanceTimer_ != nullptr && maintenanceTimer_->isActive()) {
        startEventWaiter();
    }
}

void SessionWorker::sync()
{
    drainMailbox();
    settleDrain();
}

void SessionWorker::drainMailbox()
{
    if (!session_ || !session_->isConnected()) {
        return;
    }
    const bazarish::log::Slow timed("reading the mailbox", kSlowPass);
    std::vector<IncomingMessage> messages;
    try {
        try {
            messages = session_->sync(false, Session::kPendingItemsPerPass);
        } catch (const bazarish::client::ApiError& error) {
            if (error.code != bazarish::ErrorCode::eClientUnregistered
                || nowMillis() - lastRegisterAttemptMs_ < kRegisterRetryIntervalMs) {
                throw;
            }
            lastRegisterAttemptMs_ = nowMillis();
            bazarish::log::info("this device is not registered with the server: registering it");
            session_->registerAccount();
            messages = session_->sync(false, Session::kPendingItemsPerPass);
        }
        emit syncReachable(true, {});
    } catch (const std::exception& error) {
        bazarish::log::warn("sync failed: {}", error.what());
        emit syncReachable(false, QString::fromUtf8(error.what()));
        return;
    }
    drainResolvedAdds();
    for (const IncomingMessage& m : messages) {
        if (m.contentType == "avatar") {
            emit avatarReady(QString::fromStdString(m.fromFingerprint),
                QByteArray(m.avatarData.data(), static_cast<int>(m.avatarData.size())));
            ackPending(QString::fromStdString(m.pendingId));
            continue;
        }
        if (m.contentType == "device.avatar") {
            emit avatarReady(QString::fromStdString(session_->fingerprint()),
                QByteArray(m.avatarData.data(), static_cast<int>(m.avatarData.size())));
            ackPending(QString::fromStdString(m.pendingId));
            continue;
        }
        if (m.contentType == "device.contact-name" || m.contentType == "device.i2p-master") {
            ackPending(QString::fromStdString(m.pendingId));
            continue;
        }
        if (m.contentType == "file.request" || m.contentType == "file.offer"
            || m.contentType == "file.unavailable" || m.contentType == "file.cancel") {
            ackPending(QString::fromStdString(m.pendingId));
            continue;
        }
        QVariantMap map;
        map["peer"] = QString::fromStdString(m.fromFingerprint);
        map["sentByUs"] = m.sentByUs;
        map["type"] = QString::fromStdString(m.contentType);
        map["text"] = QString::fromStdString(m.text);
        map["rawType"] = QString::fromStdString(m.rawType);
        map["established"] = m.establishedContact;
        map["attName"] = QString::fromStdString(m.attachmentName);
        map["attMime"] = QString::fromStdString(m.attachmentMime);
        map["attSize"] = static_cast<qint64>(m.attachmentSize);
        map["attDurationMs"] = static_cast<qint64>(m.attachmentDurationMs);
        if (m.contentType == "audio") {
            const std::optional<Bytes> audio = session_->voice(m.e2eId);
            if (audio.has_value()) {
                map["attWave"] = waveformHex(*audio);
            }
        }
        map["attRef"] = QString::fromStdString(m.attachmentRef);
        map["keyboard"] = QString::fromStdString(m.keyboardJson);
        map["e2eId"] = QString::fromStdString(m.e2eId);
        map["forwarded"] = m.forwarded;
        map["ref"] = QString::fromStdString(m.refId);
        map["replyTo"] = QString::fromStdString(m.replyTo);
        map["sentAt"] = static_cast<qint64>(m.sentAt);
        map["pendingId"] = QString::fromStdString(m.pendingId);
        emit messageReceived(map);
    }
    emitContacts();
    emitFacadeInfo();
    refreshCalls();
}

void SessionWorker::refreshCalls()
{
    if (!session_) {
        return;
    }
    session_->tickCalls();
    flushCallLog();
    emitCallState();
}

void SessionWorker::maintain()
{
    if (!session_ || !session_->isConnected()) {
        return;
    }
    const bazarish::log::Slow timed("the maintenance pass", kSlowPass);
    drainResolvedAdds();
    drainAliasErrands();
    {
        const bazarish::log::Slow timedCalls("keeping the calls current", kSlowStretch);
        refreshCalls();
    }
    try {
        const bazarish::log::Slow timedEchoes("telling our own devices", kSlowStretch);
        session_->flushPendingEchoes();
    } catch (const std::exception& error) {
        bazarish::log::warn("self-sync of our own sends failed: {}", error.what());
    }
    if (session_->approvalState().pending
        && nowMillis() - lastApprovalCheckMs_ >= kApprovalCheckIntervalMs) {
        lastApprovalCheckMs_ = nowMillis();
        try {
            (void)session_->i2pDestStatus();
            if (!session_->approvalState().pending) {
                session_->publishRouting();
                refreshI2pStatus();
            }
        } catch (const std::exception& error) {
            bazarish::log::warn("approval check failed: {}", error.what());
        }
        const Session::ApprovalState approval = session_->approvalState();
        emit approvalState(approval.pending, QString::fromStdString(approval.message));
    }
    if (nowMillis() - lastTransientCheckMs_ >= kTransientCheckIntervalMs) {
        lastTransientCheckMs_ = nowMillis();
        try {
            const qint64 jitter
                = static_cast<qint64>(bazarish::randomBytes(1)[0]) * kTransientJitterSeconds / 255;
            if (session_->refreshI2pTransientIfDue(
                    QDateTime::currentSecsSinceEpoch(), kTransientRenewLeadSeconds - jitter)) {
                bazarish::log::info("delegation re-issued for this account");
                refreshI2pStatus();
            }
        } catch (const std::exception& error) {
            bazarish::log::warn("delegation renewal check failed: {}", error.what());
        }
    }
    if (nowMillis() - lastAliasServiceMs_ >= kAliasServiceIntervalMs
        && session_->aliasServicingDue()) {
        lastAliasServiceMs_ = nowMillis();
        startAliasErrand(/*byHand=*/false);
    }
}

void SessionWorker::emitCallState()
{
    if (!session_) {
        return;
    }
    const Session::CallInfo call = session_->currentCall();
    emit callStateChanged(static_cast<int>(call.state), QString::fromStdString(call.peerFingerprint),
        QString::fromStdString(call.callId), call.muted, QString::fromStdString(call.stage),
        call.peerRinging, static_cast<qint64>(call.connectedAtMs), call.inputLevel,
        call.outputLevel);
    reconcileCallTimer();
}

void SessionWorker::reconcileCallTimer()
{
    const bool live = session_ && session_->currentCall().state != Session::CallState::eIdle;
    if (live) {
        if (callTimer_ == nullptr) {
            callTimer_ = new QTimer(this);
            callTimer_->setInterval(kCallWatchIntervalMs);
            connect(callTimer_, &QTimer::timeout, this, &SessionWorker::emitCallState);
        }
        if (!callTimer_->isActive()) {
            callTimer_->start();
        }
    } else if (callTimer_ != nullptr) {
        callTimer_->stop();
    }
}

void SessionWorker::flushCallLog()
{
    if (!session_) {
        return;
    }
    for (const Session::CompletedCall& call : session_->takeCallLog()) {
        emit callLogged(QString::fromStdString(call.peer), call.incoming,
            static_cast<int>(call.outcome), static_cast<qint64>(call.durationSec));
    }
}

void SessionWorker::startCall(const QString& peer)
{
    if (!session_) {
        return;
    }
    try {
        session_->startAudioCall(peer.toStdString());
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
    } catch (const std::exception& error) {
        bazarish::log::warn("decline failed: {}", error.what());
    }
    emitCallState();
    flushCallLog();
}

void SessionWorker::endCall()
{
    if (!session_) {
        return;
    }
    try {
        session_->endCall();
    } catch (const std::exception& error) {
        bazarish::log::warn("hang-up failed: {}", error.what());
    }
    emitCallState();
    flushCallLog();
}

void SessionWorker::setCallMuted(const bool muted)
{
    if (!session_) {
        return;
    }
    session_->setCallMuted(muted);
    emitCallState();
}

void SessionWorker::sendText(const QString& peer, const QString& text, qint64 localId,
    const QString& e2eId, const QString& replyTo, const bool forwarded)
{
    try {
        session_->sendMessage(peer.toStdString(), text.toStdString(), e2eId.toStdString(),
            watchFor(this, localId), replyTo.toStdString(), forwarded);
    } catch (const std::exception& e) {
        emit sendResult(localId, false, QString::fromUtf8(e.what()));
    }
}

void SessionWorker::sendFile(const QString& peer, const QString& localPath, qint64 localId,
    const QString& e2eId, const QString& replyTo)
{
    try {
        session_->sendFile(peer.toStdString(), localPath.toStdString(), e2eId.toStdString(),
            watchFor(this, localId), replyTo.toStdString());
    } catch (const std::exception& e) {
        emit sendResult(localId, false, QString::fromUtf8(e.what()));
    }
}

void SessionWorker::sendVoice(const QString& peer, const QByteArray& opus,
    const qint64 durationMs, const qint64 localId, const QString& e2eId,
    const QString& replyTo, const bool forwarded)
{
    try {
        const Bytes audio(opus.begin(), opus.end());
        session_->sendVoice(peer.toStdString(), audio, durationMs, e2eId.toStdString(),
            watchFor(this, localId), replyTo.toStdString(), forwarded);
    } catch (const std::exception& e) {
        emit sendResult(localId, false, QString::fromUtf8(e.what()));
    }
}

void SessionWorker::sendPicture(const QString& peer, const QByteArray& bytes,
    const QString& name, const QString& mime, qint64 localId, const QString& e2eId,
    const QString& replyTo)
{
    try {
        session_->sendPicture(peer.toStdString(),
            bazarish::Bytes(bytes.begin(), bytes.end()), name.toStdString(), mime.toStdString(),
            e2eId.toStdString(), watchFor(this, localId), replyTo.toStdString());
    } catch (const std::exception& e) {
        emit sendResult(localId, false, QString::fromUtf8(e.what()));
    }
}

void SessionWorker::sendReceipt(const QString& peer, const QString& refId)
{
    const QString op = beginOp(
        QStringLiteral("service"), tr("Read receipt"), tr("Sending…"));
    try {
        session_->sendReceipt(peer.toStdString(), refId.toStdString());
        emit opDone(op, true, tr("Receipt sent"));
    } catch (const std::exception&) {
        emit opDone(op, false, tr("Receipt not sent"));
    }
}

void SessionWorker::queueErrand(Errand errand)
{
    startErrands();
    {
        const std::lock_guard<std::mutex> lock(errandMutex_);
        errandQueue_.push_back(std::move(errand));
    }
    errandWake_.notify_one();
}

void SessionWorker::queueAck(const std::string& pendingId)
{
    Errand errand;
    errand.pendingId = pendingId;
    queueErrand(std::move(errand));
}

void SessionWorker::ackPending(const QString& pendingId)
{
    if (!session_ || pendingId.isEmpty()) {
        return;
    }
    queueAck(pendingId.toStdString());
}

void SessionWorker::sendReaction(const QString& peer, const QString& refId, const QString& emoji)
{
    const QString op = beginOp(QStringLiteral("service"),
        emoji.isEmpty() ? tr("Removing reaction") : tr("Reaction %1").arg(emoji),
        tr("Sending…"));
    try {
        session_->sendReaction(peer.toStdString(), refId.toStdString(), emoji.toStdString());
        emit opDone(op, true, tr("Accepted by your server"));
    } catch (const std::exception& error) {
        bazarish::log::warn("reaction not sent: {}", error.what());
        emit opDone(op, false, tr("Not sent: %1").arg(QString::fromUtf8(error.what())));
    }
}

void SessionWorker::sendCallback(
    const QString& opId, const QString& peer, const QString& data, const QString& ref)
{
    try {
        session_->sendCallback(peer.toStdString(), data.toStdString(), ref.toStdString());
        emit botActionDone(opId, true, {});
    } catch (const std::exception& e) {
        emit botActionDone(opId, false, QString::fromUtf8(e.what()));
    }
}

void SessionWorker::sendCommand(
    const QString& opId, const QString& peer, const QString& command, const QString& args)
{
    try {
        session_->sendCommand(peer.toStdString(), command.toStdString(), args.toStdString());
        emit botActionDone(opId, true, {});
    } catch (const std::exception& e) {
        emit botActionDone(opId, false, QString::fromUtf8(e.what()));
    }
}

void SessionWorker::sendEdit(
    const QString& peer, const QString& refId, qint64 localId, const QString& text)
{
    try {
        session_->sendEdit(peer.toStdString(), refId.toStdString(), text.toStdString(), {},
            watchFor(this, localId));
    } catch (const std::exception& e) {
        emit sendResult(localId, false, QString::fromUtf8(e.what()));
    }
}

void SessionWorker::dropSentFile(const QString& refId)
{
    try {
        session_->unsend(refId.toStdString());
    } catch (const std::exception& error) {
        bazarish::log::debug("nothing to forget for a deleted message: {}", error.what());
    }
}

void SessionWorker::sendDelete(const QString& peer, const QString& refId)
{
    try {
        try {
            session_->unsend(refId.toStdString());
        } catch (const std::exception& error) {
            bazarish::log::debug("unsend before delete did nothing: {}", error.what());
        }
        session_->sendDelete(peer.toStdString(), refId.toStdString());
    } catch (const std::exception& e) {
        emit actionFailed(QString::fromUtf8(e.what()));
    }
}

QString SessionWorker::coreText(const QString& reported)
{
    for (const char* const known : kCoreProgress) {
        if (reported == QLatin1StringView(known)) {
            return tr(known);
        }
    }
    return reported;
}

QString SessionWorker::beginOp(const QString& kind, const QString& title, const QString& status)
{
    const QString opId = QStringLiteral("op:") + QString::number(++opSeq_);
    emit opBegin(opId, kind, title, status);
    return opId;
}

void SessionWorker::addByInvite(const QString& uri, const QString& intro, const QString& opId,
    const QString& requestId)
{
    startContactAdd(/*byAlias=*/false, uri, intro, opId, requestId);
}

void SessionWorker::addByAlias(const QString& alias, const QString& intro, const QString& opId)
{
    startContactAdd(/*byAlias=*/true, alias, intro, opId);
}

void SessionWorker::startContactAdd(const bool byAlias, const QString& uriOrAlias,
    const QString& intro, const QString& opId, const QString& requestId)
{
    if (!session_) {
        emit contactAddDone(opId, false, tr("no account open"));
        emit actionFailed(tr("no account open"));
        return;
    }
    bazarish::client::Session::ContactFetchContext context;
    try {
        context = session_->contactFetchContext();
    } catch (const std::exception& e) {
        emit contactAddDone(opId, false, QString::fromUtf8(e.what()));
        emit actionFailed(QString::fromUtf8(e.what()));
        return;
    }
    bazarish::client::Session::ContactCardRequest request;
    request.byAlias = byAlias;
    request.uriOrAlias = uriOrAlias.toStdString();
    request.introText = intro.toStdString();
    request.requestId = requestId.isEmpty()
        ? bazarish::toHex(bazarish::randomBytes(kContactRequestIdBytes))
        : requestId.toStdString();
    try {
        session_->notePendingContactAdd({opId.toStdString(), request});
    } catch (const std::exception& e) {
        bazarish::log::warn("contact-add not recorded: {}", e.what());
    }
    emit contactAddStage(opId, tr("Resolving recipient over I2P…"));

    if (!resolvedAdds_) {
        resolvedAdds_ = std::make_shared<ResolvedContactAddQueue>();
    }
    std::shared_ptr<ResolvedContactAddQueue> queue = resolvedAdds_;
    try {
        std::thread([context = std::move(context), request = std::move(request),
                        queue = std::move(queue), opId]() {
            bazarish::client::tellFetchStages([queue, opId](const std::string& stage) {
                const std::lock_guard<std::mutex> lock(queue->mutex);
                queue->stages.push_back({opId, QString::fromStdString(stage) + QStringLiteral("…")});
            });
            bazarish::client::Session::ContactCardResolved resolved
                = bazarish::client::Session::resolveContactCard(context, request);
            bazarish::client::tellFetchStages(nullptr);
            const std::lock_guard<std::mutex> lock(queue->mutex);
            queue->results.push_back({opId, std::move(resolved)});
        }).detach();
    } catch (const std::exception& e) {
        emit contactAddDone(opId, false, QString::fromUtf8(e.what()));
        emit actionFailed(QString::fromUtf8(e.what()));
    }
}

void SessionWorker::forgetPendingAdd(const QString& opId)
{
    if (!session_) {
        return;
    }
    session_->forgetPendingContactAdd(opId.toStdString());
}

void SessionWorker::resumePendingAdds()
{
    if (!session_) {
        return;
    }
    for (const bazarish::client::Session::PendingContactAdd& pending :
        session_->pendingContactAdds()) {
        const QString opId = QString::fromStdString(pending.opId);
        emit opBegin(opId, QStringLiteral("contact"), tr("Adding a contact"),
            tr("Resuming after a restart…"));
        startContactAdd(pending.request.byAlias,
            QString::fromStdString(pending.request.uriOrAlias),
            QString::fromStdString(pending.request.introText), opId,
            QString::fromStdString(pending.request.requestId));
    }
}

void SessionWorker::drainResolvedAdds()
{
    if (!session_ || !resolvedAdds_) {
        return;
    }
    std::vector<ResolvedContactAddQueue::Entry> ready;
    std::vector<std::pair<QString, QString>> stages;
    {
        const std::lock_guard<std::mutex> lock(resolvedAdds_->mutex);
        ready.swap(resolvedAdds_->results);
        stages.swap(resolvedAdds_->stages);
    }
    for (const auto& [opId, stage] : stages) {
        emit contactAddStage(opId, stage);
    }
    for (const ResolvedContactAddQueue::Entry& entry : ready) {
        const bazarish::client::Session::ContactCardResolved& resolved = entry.resolved;
        if (!resolved.ok) {
            emit contactAddDone(entry.opId, false, QString::fromStdString(resolved.error));
            emit actionFailed(QString::fromStdString(resolved.error));
            continue;
        }
        if (session_->hasContact(resolved.fingerprint)) {
            emit contactAlreadyKnown(
                entry.opId, QString::fromStdString(resolved.fingerprint));
            continue;
        }
        try {
            emit contactAddStage(entry.opId, tr("Sending request…"));
            const std::string fingerprint = session_->commitContactAdd(resolved);
            emit actionOk(tr("Contact request sent"));
            emit contactRequestSent(QString::fromStdString(fingerprint),
                QString::fromStdString(resolved.introText),
                QString::fromStdString(resolved.requestId));
            emit contactAddDone(
                entry.opId, true, tr("Request sent, awaiting delivery…"));
        } catch (const bazarish::client::ApiError& e) {
            if (e.code == bazarish::ErrorCode::eContactRateLimited) {
                emit contactAddRateLimited(entry.opId,
                    QString::fromStdString(resolved.fingerprint),
                    QString::fromStdString(resolved.requestId));
                continue;
            }
            emit contactAddDone(entry.opId, false, QString::fromUtf8(e.what()));
            emit actionFailed(QString::fromUtf8(e.what()));
        } catch (const std::exception& e) {
            emit contactAddDone(entry.opId, false, QString::fromUtf8(e.what()));
            emit actionFailed(QString::fromUtf8(e.what()));
        }
    }
}

void SessionWorker::noteCommandDone()
{
    emit commandFinished();
}

void SessionWorker::acceptContact(const QString& peer)
{
    WorkerOp op(this, QStringLiteral("accept:") + peer, QStringLiteral("contact"),
        tr("Agreeing to a contact request"), tr("Telling your server…"));
    try {
        session_->acceptContactRequest(peer.toStdString());
        op.succeed(tr("Agreed"));
        emit contactAccepted(peer, true, {});
        emitContacts();
        sync();
    } catch (const std::exception& e) {
        op.fail(QString::fromUtf8(e.what()));
        emit contactAccepted(peer, false, QString::fromUtf8(e.what()));
        emit actionFailed(QString::fromUtf8(e.what()));
    }
}

void SessionWorker::requestInvite()
{
    try {
        if (!session_->hasOwnRouting()) {
            WorkerOp op(this, QStringLiteral("refresh-card"), QStringLiteral("dest"),
                tr("Refreshing your contact card"),
                tr("Asking your server for your destination…"));
            session_->refreshOwnCard();
            op.succeed(session_->hasOwnRouting() ? tr("Card updated")
                                                 : tr("Destination not up yet"));
        }
        emit inviteReady(QString::fromStdString(session_->inviteUri()));
    } catch (const std::exception& e) {
        emit inviteUnavailable(QString::fromUtf8(e.what()));
    }
}

void SessionWorker::setAvatar(const QImage& image)
{
    if (!session_) {
        return;
    }
    const QString op = beginOp(
        QStringLiteral("service"), tr("Setting your avatar"), tr("Preparing the image…"));
    try {
        const QByteArray bytes = compressAvatarJpeg(image);
        if (bytes.isEmpty()) {
            emit opDone(op, false, tr("Could not read the image"));
            emit actionFailed(tr("Could not read the selected image."));
            return;
        }
        emit opProgress(op, tr("Sending to your contacts…"));
        session_->setAvatar(bazarish::Bytes(bytes.begin(), bytes.end()), "image/jpeg");
        emit avatarReady(QString::fromStdString(session_->fingerprint()), bytes);
        emit opDone(op, true, tr("Avatar set"));
    } catch (const std::exception& e) {
        emit opDone(op, false, QString::fromUtf8(e.what()));
        emit actionFailed(QString::fromUtf8(e.what()));
    }
}

void SessionWorker::clearAvatar()
{
    if (!session_) {
        return;
    }
    const QString op
        = beginOp(QStringLiteral("service"), tr("Removing your avatar"), tr("Working…"));
    try {
        session_->setAvatar({}, {});
        emit avatarReady(QString::fromStdString(session_->fingerprint()), {});
        emit opDone(op, true, tr("Avatar removed"));
    } catch (const std::exception& e) {
        emit opDone(op, false, QString::fromUtf8(e.what()));
        emit actionFailed(QString::fromUtf8(e.what()));
    }
}

void SessionWorker::setDisplayName(const QString& name)
{
    withSession([&] {
        session_->setDisplayName(name.toStdString());
        emit renamed(QString::fromStdString(session_->displayName()));
        emit actionOk(tr("Name updated"));
    });
}

void SessionWorker::renameContact(const QString& peer, const QString& name)
{
    withSession([&] {
        session_->renameContact(peer.toStdString(), name.toStdString());
        emitContacts();
    });
}

void SessionWorker::emitSettings()
{
    if (!session_) {
        return;
    }
    emit accountSettings(session_->acceptCalls(), session_->sendReceipts(),
        session_->sharingAllowed());
}

void SessionWorker::syncChatClear(const QString& peer)
{
    if (!session_) {
        return;
    }
    try {
        session_->syncChatClearToSelf(peer.toStdString());
    } catch (const std::exception& error) {
        bazarish::log::warn("chat-clear self-sync failed: {}", error.what());
    }
}

void SessionWorker::syncRead(const QString& peer, const qint64 sentAtMs)
{
    if (!session_) {
        return;
    }
    try {
        session_->syncReadToSelf(peer.toStdString(), sentAtMs);
    } catch (const std::exception& error) {
        bazarish::log::warn(
            "read mark not synced to this account's other devices: {}", error.what());
    }
}

void SessionWorker::syncChatPin(const QString& peer, bool pinned)
{
    if (!session_) {
        return;
    }
    try {
        session_->syncChatPinToSelf(peer.toStdString(), pinned);
    } catch (const std::exception& error) {
        bazarish::log::warn("pin not synced to this account's other devices: {}", error.what());
    }
}

void SessionWorker::clearSaved()
{
    withSession([&] {
        session_->clearSaved();
    });
}

void SessionWorker::setBlocked(const QString& peer, const bool blocked)
{
    withSession([&] {
        session_->setBlocked(peer.toStdString(), blocked);
        emitContacts();
    });
}

void SessionWorker::setContactNotifications(const QString& peer, const bool on)
{
    withSession([&] {
        session_->setContactNotifications(peer.toStdString(), on);
        emitContacts();
    });
}

void SessionWorker::setContactCalls(const QString& peer, const bool allowed)
{
    withSession([&] {
        session_->setContactCalls(peer.toStdString(), allowed);
        emitContacts();
    });
}

void SessionWorker::removeContact(const QString& peer)
{
    withSession([&] {
        session_->removeContactEverywhere(peer.toStdString());
        emit avatarReady(peer, QByteArray());
        emitContacts();
        emit actionOk(tr("Contact deleted"));
    });
}

void SessionWorker::clearChatForEveryone(const QString& peer)
{
    withSession([&] {
        session_->sendChatClear(peer.toStdString());
    });
}

void SessionWorker::shutdown()
{
    stopEventWaiter();
    stopErrands();
    if (maintenanceTimer_ != nullptr) {
        maintenanceTimer_->stop();
    }
    if (callTimer_ != nullptr) {
        callTimer_->stop();
    }
    downloadsCancelled_.store(true);
    if (session_) {
        session_->releaseI2pLinks();
    }
    session_.reset();
    emit stopped();
}

void SessionWorker::askDevicesForContacts()
{
    withSession([&] {
        session_->askDevicesForContacts();
        emit actionOk(tr("Asked your other devices for your contacts"));
    });
}

void SessionWorker::connectionLog()
{
    if (!session_) {
        emit connectionLogReady({});
        return;
    }
    QVariantList lines;
    for (const bazarish::client::WireEvent& event : session_->connectionLog()) {
        lines.append(QVariantMap{
            {QStringLiteral("at"), QVariant::fromValue(event.atMillis)},
            {QStringLiteral("outgoing"), event.outgoing},
            {QStringLiteral("what"), QString::fromStdString(event.what)},
            {QStringLiteral("status"), QString::fromStdString(event.status)},
            {QStringLiteral("detail"), QString::fromStdString(event.detail)},
        });
    }
    emit connectionLogReady(lines);
}

void SessionWorker::clearConnectionLog()
{
    if (session_) {
        session_->clearConnectionLog();
    }
    emit connectionLogReady({});
}

void SessionWorker::signLogin(const QString& challenge)
{
    if (!session_) {
        emit actionFailed(tr("no account open"));
        return;
    }
    try {
        emit loginSigned(QString::fromStdString(session_->signLogin(challenge.toStdString())));
    } catch (const std::exception& e) {
        emit actionFailed(QString::fromUtf8(e.what()));
    }
}

void SessionWorker::publishThisDeviceAddress()
{
    if (!session_) {
        return;
    }
    WorkerOp op(this, QStringLiteral("i2p-address"), QStringLiteral("status"),
        tr("Publishing your address"), tr("Telling your server…"));
    try {
        session_->publishThisDeviceAddress();
        op.succeed(tr("Your server serves this address now"));
    } catch (const std::exception& error) {
        op.fail(QString::fromUtf8(error.what()));
        emit actionFailed(QString::fromUtf8(error.what()));
    }
    refreshI2pStatus();
}

void SessionWorker::publishFreshAddress()
{
    if (!session_) {
        return;
    }
    WorkerOp op(this, QStringLiteral("i2p-address"), QStringLiteral("status"),
        tr("Making a new address"), tr("Building it…"));
    try {
        session_->publishFreshAddress();
        op.succeed(tr("A new address is published"));
    } catch (const std::exception& error) {
        op.fail(QString::fromUtf8(error.what()));
        emit actionFailed(QString::fromUtf8(error.what()));
    }
    refreshI2pStatus();
}

void SessionWorker::refreshContacts()
{
    if (session_) {
        emitContacts();
    }
}

void SessionWorker::refreshI2pStatus()
{
    if (!session_) {
        return;
    }
    const bool hasKey = session_->hasI2pDestination();
    const QString address
        = hasKey ? QString::fromStdString(session_->i2pAddress()) : QString();
    emit i2pKeyState(hasKey, address);
    WorkerOp op(this, QStringLiteral("i2p-status"), QStringLiteral("status"),
        tr("Checking your destination"), tr("Asking your server…"));
    bool delegated = false;
    bool live = false;
    qint64 transientExpires = 0;
    QString summary;
    QString serverState;
    try {
        const bazarish::client::I2pDestStatus s = session_->i2pDestStatus();
        try {
            const bazarish::client::DestinationInfo served = session_->serverDestination();
            serverState = QString::fromStdString(served.state);
            emit i2pServedAddress(QString::fromStdString(served.dest));
        } catch (const std::exception& error) {
            bazarish::log::warn("destination state unavailable: {}", error.what());
        }
        transientExpires = static_cast<qint64>(s.transientExpires);
        delegated = transientExpires != 0;
        live = s.approved() && delegated;
        if (live && serverState == QStringLiteral("building")) {
            summary = tr("Delegated — your server is bringing the destination up.");
        } else if (live) {
            summary = tr("Published — your destination is live.");
        } else if (s.approval == "pending") {
            summary = s.registrationMessage.empty()
                ? tr("Awaiting operator approval — no destination until then.")
                : QString::fromStdString(s.registrationMessage);
        } else if (hasKey) {
            summary = tr("Not published — nobody can reach you yet.");
        } else {
            summary = tr("No destination key yet.");
        }
    } catch (const std::exception& error) {
        bazarish::log::warn("destination status poll failed: {}", error.what());
        summary = hasKey ? tr("Destination key ready; connect to publish it.")
                         : tr("No destination key yet.");
    }
    op.succeed(summary);
    const Session::ApprovalState approval = session_->approvalState();
    emit approvalState(approval.pending, QString::fromStdString(approval.message));
    emit i2pStatus(hasKey, delegated, live, address, summary, transientExpires, serverState);
}

void SessionWorker::refreshStorageUsage()
{
    if (!session_) {
        return;
    }
    WorkerOp op(this, QStringLiteral("storage-usage"), QStringLiteral("status"),
        tr("Checking your mailbox"), tr("Asking your server…"));
    const bazarish::client::StorageUsage u = session_->storageUsage();
    if (u.mailboxOk) {
        op.succeed(tr("Mailbox: %1 of %2")
                       .arg(humanBytes(static_cast<qint64>(u.mailboxUsedBytes)),
                           humanBytes(static_cast<qint64>(u.mailboxQuotaBytes))));
    } else {
        op.fail(tr("Your server did not answer"));
    }
    emit storageUsageReady(u.mailboxOk, static_cast<qulonglong>(u.mailboxUsedBytes),
        static_cast<qulonglong>(u.mailboxQuotaBytes));
}

void SessionWorker::refreshDevices()
{
    if (!session_) {
        return;
    }
    WorkerOp op(this, QStringLiteral("devices"), QStringLiteral("status"),
        tr("Checking your devices"), tr("Asking your server…"));
    try {
        QVariantList devices;
        for (const bazarish::client::Client::DeviceEntry& device : session_->devices()) {
            devices.append(QVariantMap{
                {QStringLiteral("clientId"), QString::fromStdString(device.clientId)},
                {QStringLiteral("current"), device.current},
                {QStringLiteral("queue"), static_cast<qulonglong>(device.queued)},
            });
        }
        op.succeed(tr("Devices: %1").arg(devices.size()));
        emit devicesReady(devices);
    } catch (const std::exception& error) {
        op.fail(QString::fromUtf8(error.what()));
        emit actionFailed(QString::fromUtf8(error.what()));
    }
}

void SessionWorker::forgetDevice(const QString& clientId)
{
    if (!session_) {
        return;
    }
    {
        WorkerOp op(this, QStringLiteral("device-forget"), QStringLiteral("status"),
            tr("Forgetting a device"), tr("Telling your server…"));
        try {
            session_->retireDevice(clientId.toStdString());
            op.succeed(tr("Forgotten"));
            emit actionOk(tr("Device forgotten. Its unread mail is no longer held"));
        } catch (const std::exception& error) {
            op.fail(QString::fromUtf8(error.what()));
            emit actionFailed(QString::fromUtf8(error.what()));
        }
    }
    refreshDevices();
}

void SessionWorker::closeAccountOnServer()
{
    if (!session_) {
        emit accountClosed(false, tr("This account is not open"));
        return;
    }
    WorkerOp op(this, QStringLiteral("account-close"), QStringLiteral("status"),
        tr("Deleting the account"), tr("Telling your server…"));
    try {
        session_->closeAccountOnServer();
        op.succeed(tr("Deleted on the server"));
        emit accountClosed(true, {});
    } catch (const bazarish::client::ApiError& error) {
        constexpr int kFirstServerErrorStatus = 500;
        if (error.httpStatus > 0 && error.httpStatus < kFirstServerErrorStatus) {
            bazarish::log::info("the server has no account of ours to end ({}); only this "
                                "device's copy goes",
                error.what());
            op.succeed(tr("Already gone from the server"));
            emit accountClosed(true, {});
            return;
        }
        op.fail(QString::fromUtf8(error.what()));
        emit accountClosed(false, QString::fromUtf8(error.what()));
    } catch (const std::exception& error) {
        op.fail(QString::fromUtf8(error.what()));
        emit accountClosed(false, QString::fromUtf8(error.what()));
    }
}

void SessionWorker::generatePersonalKey()
{
    if (!session_) {
        return;
    }
    {
        WorkerOp op(this, QStringLiteral("dest-key"), QStringLiteral("dest"),
            tr("Creating your destination key"), tr("Generating…"));
        try {
            const QString address = QString::fromStdString(session_->ensureI2pDestination());
            emit i2pKeyState(true, address);
            op.succeed(address);
            emit actionOk(tr("Personal I2P key created"));
        } catch (const std::exception& e) {
            op.fail(QString::fromUtf8(e.what()));
            emit actionFailed(QString::fromUtf8(e.what()));
        }
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
        emit actionOk(tr("Personal I2P key loaded"));
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
        emit actionOk(tr("Personal I2P key deleted"));
    } catch (const std::exception& e) {
        emit actionFailed(QString::fromUtf8(e.what()));
    }
    refreshI2pStatus();
}

void SessionWorker::setDelegationDays(const int days)
{
    withSession([&] {
        session_->setDelegationDays(days);
    });
}

void SessionWorker::setSendReceipts(const bool on)
{
    withSession([&] {
        session_->setSendReceipts(on);
    });
}

void SessionWorker::setAcceptCalls(const bool accept)
{
    withSession([&] {
        session_->setAcceptCalls(accept);
    });
}

void SessionWorker::cancelTransfer(const QString& e2eId)
{
    if (!session_) {
        return;
    }
    session_->cancelTransfer(e2eId.toStdString());
}

void SessionWorker::publishPersonalDest()
{
    if (!session_) {
        return;
    }
    {
        WorkerOp op(this, QStringLiteral("publish-dest"), QStringLiteral("dest"),
            tr("Publishing your destination"), tr("Delegating it to your server…"));
        try {
            session_->publishRouting();
            op.succeed(tr("Published"));
            emit actionOk(tr("Routing published: your card now carries this destination"));
        } catch (const std::exception& e) {
            op.fail(QString::fromUtf8(e.what()));
            emit actionFailed(QString::fromUtf8(e.what()));
        }
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
        emit actionOk(tr("I2P destination revoked"));
    } catch (const std::exception& e) {
        emit actionFailed(QString::fromUtf8(e.what()));
    }
    refreshI2pStatus();
}

void SessionWorker::saveAttachment(
    const QString& peer, const QString& e2eId, const QString& destPath, qint64 token)
{
    Session* const session = session_.get();
    if (session == nullptr) {
        emit downloadFinished(token, false, tr("No open session"));
        return;
    }
    const std::string e2eIdStd = e2eId.toStdString();
    const std::string peerStd = peer.toStdString();
    const std::string destStd = destPath.toStdString();
    downloadPool_.start([this, session, e2eIdStd, peerStd, destStd, token]() {
        try {
            session->requestFile(peerStd, e2eIdStd, destStd);
        } catch (const std::exception& e) {
            if (!downloadsCancelled_.load()) {
                emit downloadFinished(token, false, QString::fromUtf8(e.what()));
            }
        }
    });
}

void SessionWorker::reportPairing(const bazarish::client::Session::PairingEvent& event)
{
    using Stage = bazarish::client::Session::PairingStage;
    switch (event.stage) {
    case Stage::ePublishing:
        emit pairStage(tr("Publishing the address"), kProgressUnknown);
        break;
    case Stage::eWaiting:
        emit pairStage(tr("Waiting for the new device"), kProgressUnknown);
        break;
    case Stage::eWrongCode:
        emit pairStage(
            tr("Wrong code. %1 tries left").arg(bazarish::client::kMaxWrongCodes
                - event.wrongCodes),
            kProgressUnknown);
        break;
    case Stage::eSending:
        emit pairStage(tr("Sending the account"),
            event.total > 0 ? static_cast<double>(event.done) / static_cast<double>(event.total)
                            : kProgressUnknown);
        break;
    case Stage::eDone:
        emit pairFinished(true, tr("The new device has the account"));
        break;
    case Stage::eRefused:
        emit pairFinished(false,
            tr("Wrong code %1 times. The address is closed.").arg(event.wrongCodes));
        break;
    case Stage::eFailed:
        emit pairFinished(false, QString::fromStdString(event.error));
        break;
    }
}

void SessionWorker::startPairing()
{
    if (!session_) {
        return;
    }
    try {
        const bazarish::client::Session::PairingOffer offer = session_->startPairing(
            [this](const bazarish::client::Session::PairingEvent& event) {
                reportPairing(event);
            });
        emit pairOfferReady(
            QString::fromStdString(offer.uri), QString::fromStdString(offer.code));
    } catch (const std::exception& e) {
        emit pairFinished(false, QString::fromUtf8(e.what()));
    }
}

void SessionWorker::stopPairing()
{
    if (!session_) {
        return;
    }
    session_->stopPairing();
}

void SessionWorker::exportAccount(const QString& path, const QString& password)
{
    WorkerOp op(this, QStringLiteral("export"), QStringLiteral("account"),
        tr("Exporting your backup"), tr("Sealing the account…"));
    try {
        session_->exportAccount(path.toStdString(), password.toStdString());
        op.succeed(tr("Backup exported"));
        emit actionOk(tr("Backup exported"));
    } catch (const std::exception& e) {
        op.fail(QString::fromUtf8(e.what()));
        emit actionFailed(QString::fromUtf8(e.what()));
    }
}

void SessionWorker::rotateServingKey()
{
    try {
        const Session::RoutingPushResult pushed
            = session_->rotateServingKey([this](const std::string& stage) {
                  emit servingKeyStage(coreText(QString::fromStdString(stage)));
              });
        emit servingKeyDone(true,
            pushed.failed == 0
                ? tr("The key was changed. Contacts told: %1").arg(pushed.told)
                : tr("The key was changed. Told: %1, unreachable: %2")
                      .arg(pushed.told)
                      .arg(pushed.failed));
        emitContacts();
    } catch (const std::exception& e) {
        emit servingKeyDone(false, QString::fromUtf8(e.what()));
    }
}

void SessionWorker::activateAliasServicing()
{
    startAliasErrand(/*byHand=*/true);
}

void SessionWorker::startAliasErrand(const bool byHand)
{
    if (session_ == nullptr) {
        if (byHand) {
            emit aliasActivationDone(false, tr("No account is open."));
        }
        return;
    }
    if (aliasErrandRunning_) {
        aliasErrandByHand_ = aliasErrandByHand_ || byHand;
        return;
    }
    // Snapshotted here, on the thread that owns the session; the thread below touches nothing of it.
    bazarish::client::Session::AliasErrandContext context;
    try {
        context = session_->aliasErrandContext();
    } catch (const std::exception& e) {
        if (byHand) {
            emit aliasActivationDone(false, QString::fromUtf8(e.what()));
        }
        return;
    }
    if (!aliasErrands_) {
        aliasErrands_ = std::make_shared<AliasErrandQueue>();
    }
    std::shared_ptr<AliasErrandQueue> queue = aliasErrands_;
    aliasErrandRunning_ = true;
    aliasErrandByHand_ = byHand;
    try {
        std::thread([context = std::move(context), queue = std::move(queue)]() {
            bazarish::client::Session::AliasErrandResult result
                = bazarish::client::Session::runAliasErrand(context);
            const std::lock_guard<std::mutex> lock(queue->mutex);
            queue->results.push_back(std::move(result));
        }).detach();
    } catch (const std::exception& e) {
        aliasErrandRunning_ = false;
        aliasErrandByHand_ = false;
        if (byHand) {
            emit aliasActivationDone(false, QString::fromUtf8(e.what()));
        }
    }
}

void SessionWorker::drainAliasErrands()
{
    if (session_ == nullptr || !aliasErrands_) {
        return;
    }
    std::vector<bazarish::client::Session::AliasErrandResult> ready;
    {
        const std::lock_guard<std::mutex> lock(aliasErrands_->mutex);
        ready.swap(aliasErrands_->results);
    }
    for (const bazarish::client::Session::AliasErrandResult& result : ready) {
        const bool byHand = aliasErrandByHand_;
        aliasErrandRunning_ = false;
        aliasErrandByHand_ = false;
        if (!result.ok) {
            if (byHand) {
                emit aliasActivationDone(false, QString::fromUtf8(result.error.c_str()));
            } else {
                bazarish::log::info("name servicing will try again: {}", result.error);
            }
            continue;
        }
        session_->applyAliasErrand(result);
        const auto held = session_->aliasNames();
        const QVariantList rows = aliasHoldingRows(held);
        emit aliasHoldings(rows, aliasHoldingsNote(rows, session_->aliasDepositCovers()));
        if (!byHand) {
            continue;
        }
        if (held.empty()) {
            emit aliasActivationDone(true, tr("No alias is registered to this account"));
        } else if (result.pointed) {
            emit aliasActivationDone(true, tr("Your aliases now point here"));
        } else if (session_->aliasUpdatePending()) {
            emit aliasActivationDone(
                false, tr("The registry did not take the update. It will be tried again."));
        } else {
            emit aliasActivationDone(true, tr("Your aliases are up to date"));
        }
    }
}

void SessionWorker::setSharingAllowed(const bool allowed)
{
    try {
        session_->setSharingAllowed(allowed);
        emit accountSettings(session_->acceptCalls(), session_->sendReceipts(),
            session_->sharingAllowed());
    } catch (const std::exception& e) {
        emit actionFailed(QString::fromUtf8(e.what()));
    }
}

void SessionWorker::changePassphrase(const QString& passphrase)
{
    try {
        session_->changePassphrase(passphrase.toStdString());
        emit actionOk(passphrase.isEmpty() ? tr("This account is no longer password-protected")
                                           : tr("Password changed"));
    } catch (const std::exception& e) {
        emit actionFailed(QString::fromUtf8(e.what()));
    }
}

}  // namespace bazarish::app
