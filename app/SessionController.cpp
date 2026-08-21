// Bazarish project (c) 2026
#include "SessionController.hpp"

#include <QFile>

#include <QJsonDocument>

#include <QJsonArray>

#include "I2pRouter.hpp"

#include "AvatarStore.hpp"
#include "DeliveryStatus.hpp"
#include "Invite.hpp"
#include "QtAudioIo.hpp"
#include "Session.hpp"

#include <bazarish/Crypto.hpp>
#include <bazarish/Descriptor.hpp>

// Qt makes `emit` a macro and the log header declares a function of that name,
// so the keyword is stood down for the length of this include.
#pragma push_macro("emit")
#undef emit
#include <bazarish/Log.hpp>
#pragma pop_macro("emit")

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
#include <QRegularExpression>
#include <chrono>
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
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

namespace bazarish::app {

// Completed off-thread contact-card resolutions awaiting finalize on the worker
// thread (declared in the header). Shared by shared_ptr with each background
// resolve so it outlives the worker if a resolve is still running at teardown.
struct ResolvedContactAddQueue {
    std::mutex mutex;
    // Each result carries the UI operation id assigned when the add started, so the
    // finalize step can update the matching activity row.
    struct Entry {
        QString opId;
        bazarish::client::Session::ContactCardResolved resolved;
    };
    std::vector<Entry> results;
};

using bazarish::client::IncomingMessage;
using bazarish::client::ServerEndpoint;
using bazarish::client::Session;

namespace {
// Length of the "://" that separates a URL scheme from its authority.
constexpr int kSchemeSeparatorLength = 3;

// The facade as the status line shows it: host (with port, if any), without the
// scheme and without the base path. Parsing by hand rather than through QUrl,
// which reads a scheme-less "host/path" as a path with no host at all.
QString facadeHost(const QString& url)
{
    QString rest = url.trimmed();
    const int schemeEnd = rest.indexOf(QStringLiteral("://"));
    if (schemeEnd >= 0) {
        rest = rest.mid(schemeEnd + kSchemeSeparatorLength);
    }
    const int pathStart = rest.indexOf(QLatin1Char('/'));
    if (pathStart >= 0) {
        rest = rest.left(pathStart);
    }
    return rest;
}

// One background-activity row around a worker call: every server request the
// user triggers shows up in the activity panel instead of looking like a button
// that did nothing. The row ends when the scope does, whatever the exit path.
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
    QString status_ = QStringLiteral("Failed");
};

// Keeping this profile's delegation alive. The transient the server operates the
// destination with lasts 7 days, so it is re-issued about 2 days early, with a
// few hours of per-device jitter so several devices do not all issue at once
// (the poll-before-issue inside stands the losers down). Checked at most hourly:
// the check itself is a server call, and the sync tick is seconds.
constexpr qint64 kTransientCheckIntervalMs = 3600 * 1000;
constexpr qint64 kTransientRenewLeadSeconds = 5 * 24 * 3600;
constexpr qint64 kTransientJitterSeconds = 6 * 3600;

// How often the client asks its server for news when it has to poll, and how
// often it checks in when the server holds the request open for it instead.
constexpr int kPollIntervalMs = 3000;
constexpr int kHeartbeatIntervalMs = 30000;
// How long the server is asked to hold a request. Its own cap is lower; asking
// for more than it allows is answered sooner, which costs nothing.
constexpr int kEventWaitSeconds = 30;
// The floor between two waits. Without it a mailbox that still has something in
// it answers instantly every time, and the loop becomes a spin.
constexpr int kEventSettleMs = 1000;

// The background-activity row for a connect: the user can hide the progress
// dialog and still watch the connect finish in the activity panel.
const QString kConnectOperationId = QStringLiteral("connect");
constexpr double kPercentFull = 100.0;

// Unix milliseconds: the message display/order clock (sentAt is in ms).
qint64 nowMillis()
{
    return QDateTime::currentMSecsSinceEpoch();
}

// A compact human size (e.g. "1.4 MB") for transfer progress in the activity panel.
QString humanBytes(qint64 bytes)
{
    if (bytes < 1024) {
        return QString::number(bytes) + QStringLiteral(" B");
    }
    constexpr const char* kUnits[] = {"KB", "MB", "GB", "TB"};
    double value = static_cast<double>(bytes) / 1024.0;
    int unit = 0;
    while (value >= 1024.0 && unit < 3) {
        value /= 1024.0;
        ++unit;
    }
    return QString::number(value, 'f', value < 10.0 ? 1 : 0) + QChar(' ')
        + QString::fromLatin1(kUnits[unit]);
}

// Maps a server-reported federation phase to a human-readable activity status.
QString humanFederationPhase(const QString& phase)
{
    if (phase == QStringLiteral("queued")) {
        return QStringLiteral("Queued at your server…");
    }
    if (phase == QStringLiteral("dialing")) {
        // The server is finding the recipient's server on I2P and opening a
        // stream to it. Its own tunnels are usually already up, so naming this
        // "building tunnels" described the rare case and misread the common one.
        return QStringLiteral("Reaching the recipient's server…");
    }
    if (phase == QStringLiteral("connected")) {
        return QStringLiteral("Connected, sending…");
    }
    if (phase == QStringLiteral("sending")) {
        return QStringLiteral("Sending over I2P…");
    }
    if (phase == QStringLiteral("awaiting-ack")) {
        return QStringLiteral("Awaiting delivery confirmation…");
    }
    if (phase.startsWith(QStringLiteral("retry"))) {
        // "retry 3/12" -> "Retrying delivery (3/12)…"
        return QStringLiteral("Retrying delivery (") + phase.mid(6).trimmed() + QStringLiteral(")…");
    }
    return phase;
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
// older/newer step). Small on purpose: opening a chat should cost what is on
// screen, not what the chat has ever held, and the rest arrives as the user
// scrolls into it.
constexpr int kPageSize = 20;

// How many reactions outside the standard set the picker remembers.
constexpr int kRecentReactions = 10;

// The reactions offered without being asked for. Anything else a user reaches
// for - typed, or tapped on someone else's chip - is theirs, and is remembered.
const QStringList kStandardReactions = {QStringLiteral("\U0001F44D"),
    QStringLiteral("\u2764\uFE0F"), QStringLiteral("\U0001F602"),
    QStringLiteral("\U0001F389"), QStringLiteral("\U0001F525"), QStringLiteral("\U0001F62E"),
    QStringLiteral("\U0001F622"), QStringLiteral("\U0001F64F"), QStringLiteral("\U0001F440"),
    QStringLiteral("\u2705"), QStringLiteral("\U0001F4AF"), QStringLiteral("\U0001F680"),
    QStringLiteral("\U0001F621"), QStringLiteral("\U0001F44F"), QStringLiteral("\U0001F914"),
    QStringLiteral("\U0001F973"), QStringLiteral("\U0001F91D"), QStringLiteral("\U0001F529")};

// Loads a picked image and compresses it to a square JPEG within the 500 KB
// avatar protocol cap (center-crop, downscale to 256, drop quality - then, as a
// last resort, resolution - until it fits). Returns empty bytes when the file is
// not a readable image. Used for the account's own avatar.
QByteArray compressAvatarJpeg(const QString& localPath)
{
    const QImage img(localPath);
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

// ============================ SessionWorker ============================

SessionWorker::~SessionWorker()
{
    // Stop in-flight downloads before session_ (which their tasks use) is torn
    // down. Cancel first so a parked fetch closes its stream and stops retrying,
    // then wait for the pool to drain.
    downloadsCancelled_.store(true);
    downloadPool_.waitForDone();
    // The long-poll thread holds only copies and a shared flag, so it is left to
    // finish its request on its own once the flag is down.
    stopEventWaiter();
}

void SessionWorker::ensureSyncTimer()
{
    if (syncTimer_ == nullptr) {
        syncTimer_ = new QTimer(this);
        syncTimer_->setInterval(kPollIntervalMs);
        connect(syncTimer_, &QTimer::timeout, this, &SessionWorker::sync);
    }
    if (!syncTimer_->isActive()) {
        syncTimer_->start();
    }
    startEventWaiter();
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
    // The loop owns a copy of everything it touches and a shared flag, so closing
    // the profile can leave it to finish on its own.
    eventWaiter_ = std::thread([this, context, running = eventWaiterRunning_]() {
        // One client for the whole loop: each one raises an outbound destination,
        // and building a fresh one per wait meant a new dialer every time.
        std::unique_ptr<bazarish::client::Client> waiter;
        try {
            waiter = bazarish::client::Session::makeEventClient(context);
        } catch (const std::exception& error) {
            bazarish::log::warn("no event waiter: {}", error.what());
            return;
        }
        while (running->load()) {
            try {
                bazarish::client::Session::waitForEvents(*waiter, kEventWaitSeconds);
            } catch (const std::exception& error) {
                // No event face, or it went away: fall back to the timer, which
                // has been polling all along.
                bazarish::log::info("event face unavailable, polling instead: {}", error.what());
                return;
            }
            if (!running->load()) {
                return;
            }
            // Something is waiting (or the server's window closed): sync now, on
            // the worker thread where every other session call runs.
            QMetaObject::invokeMethod(this, "sync", Qt::QueuedConnection);
            // A wait that returns at once - the mailbox still holds something the
            // sync has not drained yet - would otherwise spin here and bury the
            // worker thread in syncs, which is what a send then queues behind.
            std::this_thread::sleep_for(std::chrono::milliseconds(kEventSettleMs));
        }
    });
    // With a waiter in place the timer is only a heartbeat: it catches what the
    // event face cannot report (our own outgoing state, the delegation check).
    syncTimer_->setInterval(kHeartbeatIntervalMs);
}

void SessionWorker::stopEventWaiter()
{
    if (eventWaiterRunning_) {
        eventWaiterRunning_->store(false);
    }
    if (eventWaiter_.joinable()) {
        eventWaiter_.detach();  // it ends on its own once its request returns
    }
    eventWaiterRunning_.reset();
    if (syncTimer_ != nullptr) {
        syncTimer_->setInterval(kPollIntervalMs);
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
    emitFacadeInfo();
    // Serving a file is the sender's half of a transfer, and it reports through
    // the same channel as a download. Installed once, for as long as the profile
    // is open; the download path re-points it at itself while it runs.
    session_->setTransferHandler([this](const bazarish::client::TransferEvent& event) {
        const QString id = QString::fromStdString(event.messageId);
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
            break;  // a stage, not bytes: the line above is the whole report
        }
    });
    // Local facts, before anything that touches a server: whether this profile
    // holds a destination key and at what address.
    // The invite is data this profile already holds; hand it over at open so the
    // sheet has something to show without a request.
    try {
        emit inviteReady(QString::fromStdString(session_->inviteUri()));
    } catch (const std::exception& error) {
        bazarish::log::info("no invite yet: {}", error.what());
    }
    emit i2pKeyState(session_->hasI2pDestination(),
        session_->hasI2pDestination()
            ? QString::fromStdString(session_->i2pAddress() + ".b32.i2p")
            : QString());
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
    QStringList pending;
    QStringList links;
    QStringList capacities;
    for (const std::string& fp : session_->contactFingerprints()) {
        fps << QString::fromStdString(fp);
        names << QString::fromStdString(session_->contactDisplayName(fp));
        pending << (session_->contactIsPending(fp) ? QStringLiteral("1") : QStringLiteral("0"));
        // Built here because this is where the routing is; it is the contact's
        // own card, so nothing is computed that they did not already hand over.
        try {
            links << QString::fromStdString(session_->contactInviteUri(fp));
        } catch (const std::exception&) {
            // error-hiding: allowed - a contact whose routing we have not been
            // given yet is the normal early state, and the empty entry is what
            // the card reads to say there is nothing to share.
            links << QString();
        }
        capacities << QString::number(session_->sendCapacity(fp));
    }
    emit contactsRefreshed(fps, names, pending, links, capacities);
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
        emit connectProgress(5, "Preparing");
        // The core reports its own milestones (reseed, router, tunnels, dial)
        // while the calls below block; forward them to the connect dialog.
        bazarish::client::setConnectProgressSink(
            [this](const int percent, const std::string& text) {
                emit connectProgress(percent, QString::fromStdString(text));
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
        if (endpoint.facades.empty()) {
            throw std::runtime_error("enter at least one facade URL");
        }
        // Over an I2P facade the first call builds tunnels first, so this is
        // minutes, not seconds. Say what is happening at each step.
        const bool overI2p = std::any_of(endpoint.facades.begin(), endpoint.facades.end(),
            [](const bazarish::client::Facade& f) {
                return f.host.size() > 8 && f.host.rfind(".b32.i2p") == f.host.size() - 8;
            });
        emit connectProgress(overI2p ? 8 : 20,
            overI2p ? "Connecting over I2P — the first call builds tunnels, this takes minutes"
                    : "Connecting to the server");
        session_->connectServer(endpoint);
        session_->subscribe(days);
    } catch (const std::exception& e) {
        bazarish::client::setConnectProgressSink({});
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
        } catch (const std::exception& error) {
            // No portal info reachable; fall through to the plain error.
            bazarish::log::warn("portal info unavailable: {}", error.what());
        }
        emit actionFailed(reason);
        return;
    }
    emit connectProgress(100, "Connected");
    bazarish::client::setConnectProgressSink({});
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
    } else {
        stopEventWaiter();
        if (syncTimer_ != nullptr) {
            syncTimer_->stop();
        }
        if (session_) {
            session_->releaseI2pLinks();
        }
    }
}

void SessionWorker::rebuildI2pLinks()
{
    // The waiter holds a destination of its own inside its thread; it ends once
    // its outstanding request returns and takes that destination with it. An
    // account that is offline keeps its links released rather than raising new
    // ones nobody asked for.
    stopEventWaiter();
    if (session_) {
        session_->releaseI2pLinks();
    }
    if (syncTimer_ != nullptr && syncTimer_->isActive()) {
        startEventWaiter();
    }
}

void SessionWorker::sync()
{
    if (!session_ || !session_->isConnected()) {
        return;
    }
    std::vector<IncomingMessage> messages;
    try {
        // autoAckSurfaced=false: defer acking each surfaced item until the GUI has
        // durably stored it (ackPending via ackAfterReceive), so a crash/restart
        // between fetch and store never loses a message.
        messages = session_->sync(false);
        emit syncReachable(true, {});
    } catch (const std::exception& error) {
        // Never swallowed: an account that sits at "Connecting" with no reason is
        // undiagnosable, and the reason is often nothing to do with reachability.
        bazarish::log::warn("sync failed: {}", error.what());
        emit syncReachable(false, QString::fromUtf8(error.what()));
        // Fetching the mailbox and asking after a send are separate requests, and
        // one failing says nothing about the other. Leaving this out kept every
        // message this profile had sent at "at your server" for as long as the
        // fetch kept failing - long after the recipient's server had it.
        reconcilePendingSends();
        return;  // transient (server momentarily unreachable); next tick retries
    }
    // A destination whose delegation lapses goes dark, and nothing else in the app
    // renews it: the CLI had this loop, the GUI did not.
    if (nowMillis() - lastTransientCheckMs_ >= kTransientCheckIntervalMs) {
        lastTransientCheckMs_ = nowMillis();
        try {
            const qint64 jitter
                = static_cast<qint64>(bazarish::randomBytes(1)[0]) * kTransientJitterSeconds / 255;
            if (session_->refreshI2pTransientIfDue(
                    QDateTime::currentSecsSinceEpoch(), kTransientRenewLeadSeconds - jitter)) {
                bazarish::log::info("delegation re-issued for this profile");
                refreshI2pStatus();
            }
        } catch (const std::exception& error) {
            bazarish::log::warn("delegation renewal check failed: {}", error.what());
        }
    }
    // Finalize any contact-card resolutions that completed off-thread. Done here
    // (the server is reachable, having just answered the sync above) so the
    // request delivery in commitContactAdd does not race a momentary outage.
    drainResolvedAdds();
    for (const IncomingMessage& m : messages) {
        // Avatar payloads and self-sync control messages never become chat
        // bubbles: route avatars to the store and drop the rest silently (the
        // chat list reflects name changes via the contactsRefreshed below).
        // Items the worker handles terminally (avatar routed to the store, self-sync
        // dropped) are acked here. Content messages that go to onMessageReceived are
        // NOT acked here - the controller acks them after storing (deferred ack).
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
        // The file transfer's own handshake: a request for a file we announced,
        // the offer that answers it, and the "gone" reply. The core acts on all
        // three the moment it parses them; surfacing them here put empty bubbles
        // in the conversation, one per step.
        if (m.contentType == "file.request" || m.contentType == "file.offer"
            || m.contentType == "file.unavailable") {
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
        map["attRef"] = QString::fromStdString(m.attachmentRef);
        map["attKey"] = QString::fromStdString(m.attachmentKeyB64);
        map["keyboard"] = QString::fromStdString(m.keyboardJson);
        map["messageId"] = QString::fromStdString(m.messageId);
        map["ref"] = QString::fromStdString(m.refId);
        map["replyTo"] = QString::fromStdString(m.replyTo);
        map["sentAt"] = static_cast<qint64>(m.sentAt);
        // The server-side pending id, so the controller can ack this item only after
        // it has durably stored it (deferred ack - see ackAfterReceive).
        map["pendingId"] = QString::fromStdString(m.pendingId);
        emit messageReceived(map);
    }
    emitContacts();
    emitFacadeInfo();
    // Resolve any sends still in flight from earlier (late delivery or failure).
    reconcilePendingSends();
    // Advance call ring/answer timeouts so a call never rings forever, then flush any
    // finished-call chat-history entries (peer hang-ups handled during the pull above,
    // timeouts here).
    session_->tickCalls();
    flushCallLog();
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
        bazarish::client::Session::AttemptOutcome outcome;
        try {
            outcome = session_->pollAttempt(attemptId);
        } catch (const std::exception& error) {
            // The server did not answer this poll. The attempt is still live on it,
            // so keep the message where it is and ask again next round.
            bazarish::log::warn("send status unavailable: {}", error.what());
            continue;
        }
        if (outcome.status == "delivered") {
            emit sendProgress(localId, DeliveryStatus::AtRecipientServer);  // grey -> yellow
            resolved.push_back(localId);
        } else if (outcome.status == "pending" && !outcome.phase.empty()) {
            // Still in flight: surface the server's real federation phase so the
            // activity panel reads as progress, not a frozen "sending".
            emit sendPhase(localId, QString::fromStdString(outcome.phase));
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
            emit sendSettled(localId, QString::fromStdString(outcome.status));
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
        QString::fromStdString(call.callId), call.muted, QString::fromStdString(call.stage),
        static_cast<qint64>(call.connectedAtMs));
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
    const QString& protocolId, const QString& replyTo)
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
            &attemptId, replyTo.toStdString());
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
    const QString& protocolId, const QString& replyTo)
{
    try {
        std::string attemptId;
        const bool delivered = session_->sendFile(peer.toStdString(), localPath.toStdString(),
            protocolId.toStdString(),
            [this, localId]() { emit sendProgress(localId, DeliveryStatus::AtSenderServer); },
            &attemptId, replyTo.toStdString());
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
    const QString op = beginOp(
        QStringLiteral("service"), QStringLiteral("Read receipt"), QStringLiteral("Sending…"));
    try {
        session_->sendReceipt(peer.toStdString(), refId.toStdString());
        emit opDone(op, true, QStringLiteral("Receipt sent"));
    } catch (const std::exception&) {
        // A failed receipt is non-fatal; the sender simply stays at "yellow".
        emit opDone(op, false, QStringLiteral("Receipt not sent"));
    }
}

void SessionWorker::ackPending(const QString& pendingId)
{
    if (!session_ || pendingId.isEmpty()) {
        return;
    }
    try {
        session_->ackPending(pendingId.toStdString());
    } catch (const std::exception& error) {
        // The server was momentarily unreachable: leave the item un-acked so the
        // next sync re-offers it (the GUI dedups by messageId, so no duplicate).
        bazarish::log::warn("pending item not acked: {}", error.what());
    }
}

void SessionWorker::sendReaction(const QString& peer, const QString& refId, const QString& emoji)
{
    const QString op = beginOp(QStringLiteral("service"),
        emoji.isEmpty() ? QStringLiteral("Removing reaction") : (QStringLiteral("Reaction ") + emoji),
        QStringLiteral("Sending…"));
    try {
        session_->sendReaction(peer.toStdString(), refId.toStdString(), emoji.toStdString());
        emit opDone(op, true, QStringLiteral("Accepted by your server"));
    } catch (const std::exception& error) {
        // The reaction the user set stands locally either way, but why it did not
        // reach the contact is theirs to know - the row said "Not sent" and kept
        // the reason to itself.
        bazarish::log::warn("reaction not sent: {}", error.what());
        emit opDone(op, false, QStringLiteral("Not sent: ") + QString::fromUtf8(error.what()));
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
        } catch (const std::exception& error) {
            // No blob recorded for this id (a plain text message), or the reclaim
            // failed: the blob also reclaims via its TTL. Not fatal to the delete.
            bazarish::log::debug("unsend before delete did nothing: {}", error.what());
        }
        session_->sendDelete(peer.toStdString(), refId.toStdString());
    } catch (const std::exception& e) {
        emit actionFailed(QString::fromUtf8(e.what()));
    }
}

QString SessionWorker::beginOp(const QString& kind, const QString& title, const QString& status)
{
    const QString opId = QStringLiteral("op:") + QString::number(++opSeq_);
    emit opBegin(opId, kind, title, status);
    return opId;
}

void SessionWorker::addByInvite(const QString& uri, const QString& intro, const QString& opId)
{
    // Asynchronous: the slow federated card fetch runs off this thread, so sync and
    // the connection are never blocked. The fingerprint (for out-of-band
    // verification) is surfaced when the resolve finalizes (drainResolvedAdds).
    startContactAdd(/*byUsername=*/false, uri, intro, opId);
}

void SessionWorker::addByUsername(const QString& alias, const QString& intro, const QString& opId)
{
    // Asynchronous, like addByInvite. The alias->fingerprint binding is the one
    // residual trust of the name path; the resolved fingerprint is surfaced for
    // out-of-band verification when the resolve finalizes.
    startContactAdd(/*byUsername=*/true, alias, intro, opId);
}

void SessionWorker::startContactAdd(const bool byUsername, const QString& uriOrAlias,
    const QString& intro, const QString& opId)
{
    if (!session_) {
        emit contactAddDone(opId, false, QStringLiteral("no profile open"));
        emit actionFailed(QStringLiteral("no profile open"));
        return;
    }
    // Snapshot the transport context on this (worker) thread; resolveContactCard
    // below touches no session state, so it is safe to run on a detached thread.
    bazarish::client::Session::ContactFetchContext context;
    try {
        context = session_->contactFetchContext();
    } catch (const std::exception& e) {
        emit contactAddDone(opId, false, QString::fromUtf8(e.what()));
        emit actionFailed(QString::fromUtf8(e.what()));
        return;
    }
    bazarish::client::Session::ContactCardRequest request;
    request.byUsername = byUsername;
    request.uriOrAlias = uriOrAlias.toStdString();
    request.introText = intro.toStdString();
    // The actual i2p work (build a transient tunnel, dial, fetch the card) happens
    // inside the detached resolve below; surface that we are now in it so the
    // activity panel shows real progress instead of a frozen UI.
    emit contactAddStage(opId, QStringLiteral("Resolving recipient over I2P…"));

    if (!resolvedAdds_) {
        resolvedAdds_ = std::make_shared<ResolvedContactAddQueue>();
    }
    // The detached thread captures only copies and a shared_ptr to the result
    // queue - never session_ or this - so it is safe even if the profile is closed
    // while the fetch is in flight. The worker finalizes the result on a later sync.
    std::shared_ptr<ResolvedContactAddQueue> queue = resolvedAdds_;
    try {
        std::thread([context = std::move(context), request = std::move(request),
                        queue = std::move(queue), opId]() {
            bazarish::client::Session::ContactCardResolved resolved
                = bazarish::client::Session::resolveContactCard(context, request);
            const std::lock_guard<std::mutex> lock(queue->mutex);
            queue->results.push_back({opId, std::move(resolved)});
        }).detach();
    } catch (const std::exception& e) {
        emit contactAddDone(opId, false, QString::fromUtf8(e.what()));
        emit actionFailed(QString::fromUtf8(e.what()));
    }
}

void SessionWorker::drainResolvedAdds()
{
    if (!session_ || !resolvedAdds_) {
        return;
    }
    std::vector<ResolvedContactAddQueue::Entry> ready;
    {
        const std::lock_guard<std::mutex> lock(resolvedAdds_->mutex);
        ready.swap(resolvedAdds_->results);
    }
    for (const ResolvedContactAddQueue::Entry& entry : ready) {
        const bazarish::client::Session::ContactCardResolved& resolved = entry.resolved;
        if (!resolved.ok) {
            emit contactAddDone(entry.opId, false, QString::fromStdString(resolved.error));
            emit actionFailed(QString::fromStdString(resolved.error));
            continue;
        }
        try {
            emit contactAddStage(entry.opId, QStringLiteral("Sending request…"));
            const std::string fingerprint = session_->commitContactAdd(resolved);
            emit actionOk(QString::fromStdString(
                "Contact request sent. Verify fingerprint: " + fingerprint));
            emit contactRequestSent(QString::fromStdString(fingerprint),
                QString::fromStdString(resolved.introText));
            emit contactAddDone(
                entry.opId, true, QStringLiteral("Request sent, awaiting delivery…"));
        } catch (const std::exception& e) {
            emit contactAddDone(entry.opId, false, QString::fromUtf8(e.what()));
            emit actionFailed(QString::fromUtf8(e.what()));
        }
    }
}

void SessionWorker::acceptContact(const QString& peer)
{
    WorkerOp op(this, QStringLiteral("accept:") + peer, QStringLiteral("contact"),
        QStringLiteral("Agreeing to a contact request"), QStringLiteral("Telling your server…"));
    try {
        session_->acceptContactRequest(peer.toStdString());
        op.succeed(QStringLiteral("Agreed"));
        emit contactAccepted(peer, true, {});
        emitContacts();  // issuedToThem flipped: the contact is no longer pending
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
            // The card was stored before the server had raised this account's
            // destination, so it carries no routing even though the destination
            // is up now. One grant-free re-issue picks it up.
            WorkerOp op(this, QStringLiteral("refresh-card"), QStringLiteral("dest"),
                QStringLiteral("Refreshing your contact card"),
                QStringLiteral("Asking your server for your destination…"));
            session_->refreshOwnCard();
            op.succeed(session_->hasOwnRouting() ? QStringLiteral("Card updated")
                                                 : QStringLiteral("Destination not up yet"));
        }
        emit inviteReady(QString::fromStdString(session_->inviteUri()));
    } catch (const std::exception& e) {
        // An invite with no routing in it is useless, so the sheet shows the
        // reason and the way out (publish the destination) instead of a blank.
        emit inviteUnavailable(QString::fromUtf8(e.what()));
    }
}

void SessionWorker::setAvatar(const QString& localPath)
{
    if (!session_) {
        return;
    }
    // Compressing, then handing it to every established contact, takes seconds
    // over I2P: without a row it looks like the app stopped.
    const QString op = beginOp(QStringLiteral("service"), QStringLiteral("Setting your avatar"),
        QStringLiteral("Preparing the image…"));
    try {
        const QByteArray bytes = compressAvatarJpeg(localPath);
        if (bytes.isEmpty()) {
            emit opDone(op, false, QStringLiteral("Could not read the image"));
            emit actionFailed(QStringLiteral("Could not read the selected image."));
            return;
        }
        emit opProgress(op, QStringLiteral("Sending to your contacts…"));
        session_->setAvatar(bazarish::Bytes(bytes.begin(), bytes.end()), "image/jpeg");
        // Echo locally at once so our own avatar updates without waiting for a sync.
        emit avatarReady(QString::fromStdString(session_->fingerprint()), bytes);
        emit opDone(op, true, QStringLiteral("Avatar set"));
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
    const QString op = beginOp(QStringLiteral("service"), QStringLiteral("Removing your avatar"),
        QStringLiteral("Working…"));
    try {
        session_->setAvatar({}, {});
        emit avatarReady(QString::fromStdString(session_->fingerprint()), {});
        emit opDone(op, true, QStringLiteral("Avatar removed"));
    } catch (const std::exception& e) {
        emit opDone(op, false, QString::fromUtf8(e.what()));
        emit actionFailed(QString::fromUtf8(e.what()));
    }
}

void SessionWorker::setDisplayName(const QString& name)
{
    if (!session_) {
        return;
    }
    try {
        session_->setDisplayName(name.toStdString());
        emit renamed(QString::fromStdString(session_->displayName()));
        emit actionOk(QStringLiteral("Name updated."));
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

void SessionWorker::syncChatPin(const QString& peer, bool pinned)
{
    if (!session_) {
        return;
    }
    try {
        session_->syncChatPinToSelf(peer.toStdString(), pinned);
    } catch (const std::exception& error) {
        // Best-effort device sync; the local pin already took effect.
        bazarish::log::warn("pin not synced to this account's other devices: {}", error.what());
    }
}

void SessionWorker::removeContact(const QString& peer)
{
    if (!session_) {
        return;
    }
    try {
        session_->removeContact(peer.toStdString());
        // Clear the avatar store entry and re-emit the (now shorter) contact list.
        emit avatarReady(peer, QByteArray());
        emitContacts();
        emit actionOk(QStringLiteral("Contact deleted."));
    } catch (const std::exception& e) {
        emit actionFailed(QString::fromUtf8(e.what()));
    }
}

void SessionWorker::clearChatForEveryone(const QString& peer)
{
    if (!session_) {
        return;
    }
    try {
        session_->sendChatClear(peer.toStdString());
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
    // Local facts first: the poll below can wait on a server (or on this thread
    // finishing something slow), and "is there a key" must not wait with it.
    emit i2pKeyState(hasKey, address);
    WorkerOp op(this, QStringLiteral("i2p-status"), QStringLiteral("status"),
        QStringLiteral("Checking your destination"), QStringLiteral("Asking your server…"));
    bool delegated = false;
    bool live = false;
    qint64 transientExpires = 0;
    QString summary;
    QString serverState;
    try {
        const bazarish::client::I2pDestStatus s = session_->i2pDestStatus();
        // The node holds the delegation; the messaging server is where the
        // destination is actually up or not. Both, or the status says nothing.
        try {
            serverState = QString::fromStdString(session_->serverDestination().state);
        } catch (const std::exception& error) {
            bazarish::log::warn("destination state unavailable: {}", error.what());
        }
        transientExpires = static_cast<qint64>(s.transientExpires);
        delegated = transientExpires != 0;
        live = s.approved() && delegated;
        if (live && serverState == QStringLiteral("building")) {
            summary = QStringLiteral("Delegated — your server is bringing the destination up.");
        } else if (live) {
            summary = QStringLiteral("Published — your destination is live.");
        } else if (s.approval == "pending") {
            summary = s.registrationMessage.empty()
                ? QStringLiteral("Awaiting operator approval — no destination until then.")
                : QString::fromStdString(s.registrationMessage);
        } else if (hasKey) {
            summary = QStringLiteral("Not published — nobody can reach you yet.");
        } else {
            summary = QStringLiteral("No destination key yet.");
        }
    } catch (const std::exception& error) {
        // Not connected: show what we know without the server. The status line is
        // about the destination, so the transport failure goes to the log.
        bazarish::log::warn("destination status poll failed: {}", error.what());
        summary = hasKey ? QStringLiteral("Destination key ready; connect to publish it.")
                         : QStringLiteral("No destination key yet.");
    }
    op.succeed(summary);
    emit i2pStatus(hasKey, delegated, live, address, summary, transientExpires, serverState);
}

void SessionWorker::refreshStorageUsage()
{
    if (!session_) {
        return;
    }
    WorkerOp op(this, QStringLiteral("storage-usage"), QStringLiteral("status"),
        QStringLiteral("Checking your mailbox"), QStringLiteral("Asking your server…"));
    // storageUsage never throws: a backend that did not answer comes back with
    // ok=false, which is exactly what the row must say.
    const bazarish::client::StorageUsage u = session_->storageUsage();
    if (u.mailboxOk) {
        op.succeed(QStringLiteral("Mailbox: ") + humanBytes(static_cast<qint64>(u.mailboxUsedBytes))
            + QStringLiteral(" of ") + humanBytes(static_cast<qint64>(u.mailboxQuotaBytes)));
    } else {
        op.fail(QStringLiteral("Your server did not answer"));
    }
    emit storageUsageReady(u.mailboxOk, static_cast<qulonglong>(u.mailboxUsedBytes),
        static_cast<qulonglong>(u.mailboxQuotaBytes));
}

void SessionWorker::generatePersonalKey()
{
    if (!session_) {
        return;
    }
    {
        WorkerOp op(this, QStringLiteral("dest-key"), QStringLiteral("dest"),
            QStringLiteral("Creating your destination key"), QStringLiteral("Generating…"));
        try {
            const QString address
                = QString::fromStdString(session_->ensureI2pDestination() + ".b32.i2p");
            emit i2pKeyState(true, address);
            op.succeed(address);
            emit actionOk("Personal I2P key created.");
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

void SessionWorker::allowClearnet(const bool allow)
{
    if (!session_) {
        return;
    }
    try {
        session_->setAllowClearnet(allow);
        emit actionOk(allow ? "Clearnet allowed again for this profile."
                            : "This profile is back to I2P only.");
    } catch (const std::exception& e) {
        emit actionFailed(QString::fromUtf8(e.what()));
    }
}

void SessionWorker::cancelTransfer(const QString& protocolId)
{
    if (!session_) {
        return;
    }
    session_->cancelTransfer(protocolId.toStdString());
}

void SessionWorker::publishPersonalDest()
{
    if (!session_) {
        return;
    }
    {
        WorkerOp op(this, QStringLiteral("publish-dest"), QStringLiteral("dest"),
            QStringLiteral("Publishing your destination"),
            QStringLiteral("Delegating it to your server…"));
        try {
            session_->publishRouting();
            op.succeed(QStringLiteral("Published"));
            emit actionOk("Routing published: your card now carries this destination.");
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
        emit actionOk("I2P destination revoked.");
    } catch (const std::exception& e) {
        emit actionFailed(QString::fromUtf8(e.what()));
    }
    refreshI2pStatus();
}

void SessionWorker::saveAttachment(
    const QString& peer, const QString& messageId, const QString& destPath, qint64 token)
{
    // Run the request off the worker thread (on the pool) so waiting on the peer
    // never blocks sends or sync; the transfer itself uses its own one-time I2P
    // endpoints. The task captures `this`, session_ and the cancel flag, all kept
    // alive until the pool is drained (see the destructor and openProfile). Emits
    // are skipped once cancelled, so a tearing-down session is never signalled.
    Session* const session = session_.get();
    if (session == nullptr) {
        emit downloadFinished(token, false, QStringLiteral("no open session"));
        return;
    }
    // The download is a request to the peer that announced the file, not a fetch
    // from a store, so it only completes once they answer with an offer.
    const std::string messageIdStd = messageId.toStdString();
    const std::string peerStd = peer.toStdString();
    const std::string destStd = destPath.toStdString();
    downloadPool_.start([this, session, messageIdStd, peerStd, destStd, token]() {
        try {
            // The handler installed when the profile opened reports every transfer,
            // in both directions, keyed by peer and file id. Replacing it here left
            // the receiving side with no stages and pointed a concurrent send's
            // events at this one download.
            // Returns at once: the transfer only starts when the sender answers
            // with an offer, so completion is reported by that handler.
            session->requestFile(peerStd, messageIdStd, destStd);
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
    // The chat-list search is a name-filtered view over the contacts model; the
    // source keeps its own order (pinned-first, then most-recent), which the proxy
    // preserves. An empty filter shows everything.
    contactsProxy_.setSourceModel(&contacts_);
    contactsProxy_.setFilterRole(ContactListModel::NameRole);
    contactsProxy_.setFilterCaseSensitivity(Qt::CaseInsensitive);

    worker_ = new SessionWorker();
    worker_->moveToThread(&thread_);
    connect(&thread_, &QThread::finished, worker_, &QObject::deleteLater);


    // Commands -> worker (queued across threads).
    connect(this, &SessionController::requestOpen, worker_, &SessionWorker::openProfile);
    connect(this, &SessionController::requestConnect, worker_, &SessionWorker::connectAndSubscribe);
    connect(worker_, &SessionWorker::connectProgress, this, &SessionController::onConnectProgress);
    connect(this, &SessionController::requestSendText, worker_, &SessionWorker::sendText);
    connect(this, &SessionController::requestSendFile, worker_, &SessionWorker::sendFile);
    connect(this, &SessionController::requestSendReceipt, worker_, &SessionWorker::sendReceipt);
    connect(this, &SessionController::requestSendReaction, worker_, &SessionWorker::sendReaction);
    connect(this, &SessionController::requestSendCallback, worker_, &SessionWorker::sendCallback);
    connect(this, &SessionController::requestSendCommand, worker_, &SessionWorker::sendCommand);
    connect(this, &SessionController::requestSendEdit, worker_, &SessionWorker::sendEdit);
    connect(this, &SessionController::requestSendDelete, worker_, &SessionWorker::sendDelete);
    connect(this, &SessionController::requestSetAvatar, worker_, &SessionWorker::setAvatar);
    connect(this, &SessionController::requestClearAvatar, worker_, &SessionWorker::clearAvatar);
    connect(this, &SessionController::requestSetDisplayName, worker_,
        &SessionWorker::setDisplayName);
    connect(this, &SessionController::requestRenameContact, worker_, &SessionWorker::renameContact);
    connect(this, &SessionController::requestRemoveContact, worker_, &SessionWorker::removeContact);
    connect(this, &SessionController::requestSyncChatPin, worker_, &SessionWorker::syncChatPin);
    connect(this, &SessionController::requestClearChatForEveryone, worker_,
        &SessionWorker::clearChatForEveryone);
    connect(this, &SessionController::requestAddByInvite, worker_, &SessionWorker::addByInvite);
    connect(this, &SessionController::requestAddByUsername, worker_, &SessionWorker::addByUsername);
    connect(this, &SessionController::requestAcceptContact, worker_, &SessionWorker::acceptContact);
    connect(this, &SessionController::requestInviteSig, worker_, &SessionWorker::requestInvite);
    connect(this, &SessionController::requestSignLoginSig, worker_, &SessionWorker::signLogin);
    connect(this, &SessionController::requestSaveAttachment, worker_,
        &SessionWorker::saveAttachment);
    // Download progress / outcome land on the message via the conversation model.
    connect(worker_, &SessionWorker::downloadProgress, this,
        &SessionController::onDownloadProgress);
    connect(worker_, &SessionWorker::servedProgress, this, &SessionController::onServedProgress);
    connect(worker_, &SessionWorker::transferStage, this, &SessionController::onTransferStage);
    connect(worker_, &SessionWorker::servedFinished, this, &SessionController::onServedFinished);
    connect(worker_, &SessionWorker::downloadStage, this,
        &SessionController::onDownloadStage);
    connect(worker_, &SessionWorker::downloadFinished, this,
        &SessionController::onDownloadFinished);
    connect(this, &SessionController::requestExport, worker_, &SessionWorker::exportProfile);
    connect(this, &SessionController::requestSetSync, worker_, &SessionWorker::setSyncEnabled);
    connect(this, &SessionController::requestRebuildI2p, worker_, &SessionWorker::rebuildI2pLinks);
    connect(this, &SessionController::requestCancelTransfer, worker_,
        &SessionWorker::cancelTransfer);
    connect(this, &SessionController::requestGeneratePersonalKey, worker_,
        &SessionWorker::generatePersonalKey);
    connect(this, &SessionController::requestLoadPersonalKey, worker_,
        &SessionWorker::loadPersonalKey);
    connect(this, &SessionController::requestDeletePersonalKey, worker_,
        &SessionWorker::deletePersonalKey);
    connect(this, &SessionController::requestPublishPersonalDest, worker_,
        &SessionWorker::publishPersonalDest);
    connect(this, &SessionController::requestAllowClearnet, worker_,
        &SessionWorker::allowClearnet);
    connect(this, &SessionController::requestDisablePersonalDest, worker_,
        &SessionWorker::disablePersonalDest);
    connect(this, &SessionController::requestRefreshI2pStatus, worker_,
        &SessionWorker::refreshI2pStatus);
    connect(this, &SessionController::requestRefreshStorageUsage, worker_,
        &SessionWorker::refreshStorageUsage);
    connect(this, &SessionController::requestStartCall, worker_, &SessionWorker::startCall);
    connect(this, &SessionController::requestAcceptCall, worker_, &SessionWorker::acceptCall);
    connect(this, &SessionController::requestDeclineCall, worker_, &SessionWorker::declineCall);
    connect(this, &SessionController::requestEndCall, worker_, &SessionWorker::endCall);
    connect(this, &SessionController::requestSetCallMuted, worker_, &SessionWorker::setCallMuted);

    // Results -> controller (queued).
    connect(worker_, &SessionWorker::opened, this, &SessionController::onOpened);
    connect(worker_, &SessionWorker::renamed, this, [this](const QString& newName) {
        if (newName != displayName_) {
            displayName_ = newName;
            emit identityChanged();
        }
    });
    connect(worker_, &SessionWorker::openFailed, this, &SessionController::openFailed);
    connect(worker_, &SessionWorker::connectionChanged, this,
        &SessionController::onConnectionChanged);
    connect(worker_, &SessionWorker::messageReceived, this,
        &SessionController::onMessageReceived);
    // Connected AFTER onMessageReceived (Qt invokes slots in connection order), so it
    // acks the item only once it has been durably stored - the deferred ack that
    // closes the ack-before-store window.
    connect(worker_, &SessionWorker::messageReceived, this,
        &SessionController::ackAfterReceive);
    connect(this, &SessionController::requestAckPending, worker_, &SessionWorker::ackPending);
    connect(worker_, &SessionWorker::contactsRefreshed, this,
        [this](const QStringList& fps, const QStringList& names, const QStringList& pending,
            const QStringList& links, const QStringList& capacities) {
            contactFps_ = fps;
            contactNames_.clear();
            contactLinks_.clear();
            sendCapacities_.clear();
            pendingContacts_.clear();
            for (int i = 0; i < fps.size() && i < capacities.size(); ++i) {
                sendCapacities_.insert(fps[i], capacities[i].toInt());
            }
            for (int i = 0; i < fps.size() && i < names.size(); ++i) {
                if (!names[i].isEmpty()) {
                    contactNames_.insert(fps[i], names[i]);
                }
            }
            for (int i = 0; i < fps.size() && i < links.size(); ++i) {
                if (!links[i].isEmpty()) {
                    contactLinks_.insert(fps[i], links[i]);
                }
            }
            for (int i = 0; i < fps.size() && i < pending.size(); ++i) {
                if (pending[i] == QStringLiteral("1")) {
                    pendingContacts_.insert(fps[i]);
                }
            }
            rebuildChatList();
            emit activePeerNameChanged();  // the open chat's header may have renamed
            // Re-drive any contact-request bubble's "Agree" visibility.
            ++contactsRevision_;
            emit contactsRevisionChanged();
        });
    connect(worker_, &SessionWorker::avatarReady, this, &SessionController::onAvatarReady);
    connect(worker_, &SessionWorker::sendProgress, this, &SessionController::onSendProgress);
    connect(worker_, &SessionWorker::uploadProgress, this, &SessionController::onUploadProgress);
    connect(worker_, &SessionWorker::sendResult, this, &SessionController::onSendResult);
    connect(worker_, &SessionWorker::sendSettled, this, &SessionController::onSendSettled);
    connect(worker_, &SessionWorker::sendPhase, this, &SessionController::onSendPhase);
    connect(worker_, &SessionWorker::contactRequestSent, this,
        &SessionController::onContactRequestSent);
    connect(worker_, &SessionWorker::contactAddStage, this,
        &SessionController::onContactAddStage);
    connect(worker_, &SessionWorker::contactAddDone, this, &SessionController::onContactAddDone);
    connect(worker_, &SessionWorker::contactAccepted, this,
        &SessionController::onContactAccepted);
    connect(worker_, &SessionWorker::opBegin, this, &SessionController::onOpBegin);
    connect(worker_, &SessionWorker::opDone, this, &SessionController::onOpDone);
    connect(worker_, &SessionWorker::opProgress, this,
        [this](const QString& opId, const QString& status) { updateOperation(opId, status); });
    connect(worker_, &SessionWorker::syncReachable, this, &SessionController::onSyncReachable);
    connect(worker_, &SessionWorker::facadeInfo, this, &SessionController::onFacadeInfo);
    connect(worker_, &SessionWorker::actionOk, this, &SessionController::actionOk);
    connect(worker_, &SessionWorker::actionFailed, this, [this](const QString& reason) {
        setAvatarBusy(false);
        if (connecting_) {
            connecting_ = false;
            connectPhase_.clear();
            connectError_ = reason;
            finishOperation(kConnectOperationId, false, reason);
            emit connectStateChanged();
        }
        emit actionFailed(reason);
    });
    connect(worker_, &SessionWorker::inviteReady, this, [this](const QString& uri) {
        if (ownInvite_ != uri) {
            ownInvite_ = uri;
            emit ownInviteChanged();
        }
        emit inviteReady(uri);
    });
    connect(worker_, &SessionWorker::inviteUnavailable, this,
        &SessionController::inviteUnavailable);
    connect(worker_, &SessionWorker::loginSigned, this, &SessionController::loginSigned);
    connect(worker_, &SessionWorker::serverHello, this, &SessionController::serverHello);
    connect(worker_, &SessionWorker::i2pStatus, this, &SessionController::onI2pStatus);
    connect(worker_, &SessionWorker::i2pKeyState, this, &SessionController::onI2pKeyState);
    connect(worker_, &SessionWorker::storageUsageReady, this,
        &SessionController::onStorageUsageReady);
    connect(worker_, &SessionWorker::callStateChanged, this,
        &SessionController::onCallStateChanged);
    connect(worker_, &SessionWorker::callLogged, this, &SessionController::onCallLogged);

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

// The profile's own store, opened on demand: a second connection to the same
// database the worker's session holds, which is what SQLite is built for.
client::ProfileDb& SessionController::profileDb()
{
    if (!profileDb_) {
        profileDb_ = std::make_unique<client::ProfileDb>(
            profilePath_.toStdString(), profilePassphrase_.toStdString());
    }
    return *profileDb_;
}

void SessionController::open(
    const QString& file, const QString& profileId, const QString& passphrase)
{
    profileId_ = profileId;
    profilePath_ = file;
    profilePassphrase_ = passphrase;
    // Everything a profile keeps lives in its one encrypted database; the
    // transcript is its largest table, the rest are named rows.
    store_.open(profileId, file, passphrase);
    const QJsonDocument recents = QJsonDocument::fromJson(
        QByteArray::fromStdString(profileDb().text("recent-reactions")));
    for (const QJsonValue& entry : recents.array()) {
        recentReactions_ << entry.toString();
    }
    if (!recentReactions_.isEmpty()) {
        emit recentReactionsChanged();
    }
    // There is no persistent outbound queue, so any outgoing message still at
    // "sending" is an interrupted send (the app closed mid-upload), not one in
    // flight. Mark these failed on load so they read as "not sent" with a resend
    // option, instead of a perpetual upload animation.
    store_.failUnsentOnLoad(DeliveryStatus::Sending, DeliveryStatus::Failed);
    emit requestOpen(file, passphrase);
}

void SessionController::connectServer(const QStringList& facadeUrls, const QString& serverFp)
{
    connecting_ = true;
    connectPercent_ = 0;
    connectPhase_ = QStringLiteral("Starting…");
    connectError_.clear();
    beginOperation(kConnectOperationId, QStringLiteral("connect"),
        QStringLiteral("Connecting this account"), connectPhase_);
    emit connectStateChanged();
    emit requestConnect(facadeUrls, serverFp, 14);
}

void SessionController::onConnectProgress(const int percent, const QString& phase)
{
    // Never walk backwards: the steps can repeat (every later request re-reports
    // the reseed milestone, say) and a bar or a caption that jumps back reads as
    // a fault. A repeat of the milestone we are already on still refreshes it.
    if (percent < connectPercent_) {
        return;
    }
    connectPercent_ = percent;
    connectPhase_ = phase;
    updateOperation(kConnectOperationId, phase, {}, percent / kPercentFull);
    emit connectStateChanged();
}

bool SessionController::hasI2pFacade() const
{
    for (const QString& url : configuredFacades_) {
        if (url.contains(QStringLiteral(".b32.i2p"))) {
            return true;
        }
    }
    return false;
}

QString SessionController::activeFacadeHost() const
{
    return facadeHost(activeFacade_);
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
    } catch (const std::exception& error) {
        // Malformed link: return an empty map (the caller checks).
        bazarish::log::debug("server link not parsed: {}", error.what());
    }
    return result;
}

void SessionController::activateConversation(const QString& peer)
{
    activePeer_ = peer;
    emit activePeerChanged();
    emit activePeerNameChanged();
    // The unread badge is NOT cleared on open: a message counts as read only when it
    // genuinely scrolls into the focused viewport (markReadThroughRow), so opening a
    // chat and immediately leaving does not silently swallow unread messages. Seed
    // the receipt high-water from the persistent read state so we never re-ack
    // already-read messages after a restart.
    lastReadAckedId_[peer] = qMax(lastReadAckedId_.value(peer, 0), store_.lastReadId(peer));
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
    replayTransfersForActivePeer();
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

void SessionController::closeConversation()
{
    if (activePeer_.isEmpty()) {
        return;
    }
    activePeer_.clear();
    conversation_.setMessages({});
    emit activePeerChanged();
    emit activePeerNameChanged();
}

void SessionController::openConversation(const QString& peer)
{
    activateConversation(peer);
    // Always load the newest page first so the conversation opens on its recent
    // history. When there is unread, jump to the first unread within that page; only
    // when the first unread is OLDER than a whole page do we anchor the window on it.
    // (Opening directly at the first unread used to show just the unread tail - a
    // single freshly received message - with the rest of the history collapsed
    // above, which read as "lost history" until the next open.)
    const qint64 firstUnread = store_.firstUnreadId(peer);
    loadLatestWindow();
    if (firstUnread > 0) {
        if (oldestLoadedId_ > 0 && firstUnread >= oldestLoadedId_) {
            emit scrollToUnread(firstUnread);
        } else {
            openWindowAtUnread(peer, firstUnread);
        }
    }
}

void SessionController::openWindowAtUnread(const QString& peer, qint64 firstUnread)
{
    // A page starting at the first unread message (oldest unread at the top), with
    // older read context paging in above and any further unread below.
    const QVector<StoredMessage> win = store_.newerMessages(peer, firstUnread - 1, kPageSize);
    if (win.isEmpty()) {
        loadLatestWindow();
        return;
    }
    oldestLoadedId_ = win.front().id;
    newestLoadedId_ = win.back().id;
    hasMoreOlder_ = store_.hasMessagesBefore(peer, oldestLoadedId_);
    hasMoreNewer_ = store_.hasMessagesAfter(peer, newestLoadedId_);
    conversation_.setMessages(win);
    replayTransfersForActivePeer();
    emit pagingChanged();
    emit scrollToUnread(firstUnread);
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
    replayTransfersForActivePeer();
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
    for (const SearchHit& hit : store_.searchInPeer(activePeer_, query)) {
        QVariantMap row;
        row["id"] = hit.id;
        row["text"] = hit.text;
        row["time"] = hit.ts;
        row["outgoing"] = hit.outgoing;
        row["author"] = hit.outgoing ? QStringLiteral("You") : peerName(activePeer_);
        results.push_back(row);
    }
    return results;
}

void SessionController::rebuildChatList()
{
    QVector<ContactRow> rows;
    QSet<QString> known;
    for (const QString& fp : contactFps_) {
        rows.push_back(ContactRow{fp, peerName(fp), store_.lastText(fp), store_.lastTime(fp),
            store_.unreadCount(fp), store_.isPinned(fp)});
        known.insert(fp);
    }
    // Resilience: surface a conversation whose contact record is gone but whose
    // transcript still holds messages, so a chat never silently vanishes while its
    // history persists on disk - a lost or inconsistent contact must not read as
    // data loss. Shown under the peer's name, or its short fingerprint when unknown.
    for (const QString& peer : store_.conversationPeers()) {
        if (peer.isEmpty() || known.contains(peer)) {
            continue;
        }
        rows.push_back(ContactRow{peer, peerName(peer), store_.lastText(peer),
            store_.lastTime(peer), store_.unreadCount(peer), store_.isPinned(peer)});
    }
    contacts_.setContacts(std::move(rows));
}

QString SessionController::peerName(const QString& id) const
{
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

void SessionController::setAvatar(const QString& fileOrUrl)
{
    const QUrl url(fileOrUrl);
    const QString localPath = url.isLocalFile() ? url.toLocalFile() : fileOrUrl;
    if (localPath.isEmpty() || !QFileInfo::exists(localPath)) {
        emit actionFailed(QStringLiteral("Could not read the image at ") + fileOrUrl);
        return;
    }
    setAvatarBusy(true);
    emit requestSetAvatar(localPath);
}

void SessionController::setAvatarBusy(const bool busy)
{
    if (avatarBusy_ == busy) {
        return;
    }
    avatarBusy_ = busy;
    emit avatarChanged();
}

bool SessionController::hasAvatar() const
{
    return !AvatarStore::instance().image(fingerprint_).isNull();
}

void SessionController::clearAvatar()
{
    setAvatarBusy(true);
    emit requestClearAvatar();
}

void SessionController::setDisplayName(const QString& name)
{
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty() || trimmed == displayName_) {
        return;
    }
    emit requestSetDisplayName(trimmed);
}

int SessionController::sendCapacity(const QString& fp) const
{
    return sendCapacities_.value(fp, 0);
}

QString SessionController::contactInvite(const QString& fp) const
{
    return contactLinks_.value(fp);
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

void SessionController::clearChat(bool forEveryone)
{
    if (activePeer_.isEmpty()) {
        return;
    }
    const QString peer = activePeer_;
    store_.clearPeer(peer);
    if (forEveryone) {
        emit requestClearChatForEveryone(peer);
        // A single note so the now-empty chat explains itself.
        StoredMessage sys;
        sys.peer = peer;
        sys.type = QStringLiteral("system");
        sys.text = QStringLiteral("You cleared the chat for everyone.");
        sys.ts = nowMillis();
        sys.orderKey = sys.ts;
        sys.status = DeliveryStatus::Received;
        sys.id = store_.append(sys);
    }
    // The chat row stays (clearing is not deleting); reload its window and refresh
    // the chat-list preview to the now-empty / one-line state.
    loadLatestWindow();
    contacts_.touch(peer, peerName(peer), store_.lastText(peer), store_.lastTime(peer), false);
    refreshUnreadTotal();
}

void SessionController::deleteContact()
{
    if (activePeer_.isEmpty()) {
        return;
    }
    const QString peer = activePeer_;
    // Wipe the chat and drop the contact from the list at once; the worker removes
    // it from the contact list and clears its avatar. Irreversible.
    store_.clearPeer(peer);
    contactFps_.removeAll(peer);
    contactNames_.remove(peer);
    openConversation({});  // close the conversation we just deleted
    rebuildChatList();
    refreshUnreadTotal();
    emit requestRemoveContact(peer);
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
    // Consume any reply-in-progress: the reference rides with this one message.
    const QString replyTo = replying_ ? replyingProtocolId_ : QString();
    if (replying_) {
        cancelReply();
    }
    StoredMessage m;
    m.peer = activePeer_;
    m.outgoing = true;
    m.type = "text";
    m.protocolId = SessionController_genProtocolId();
    m.text = text;
    m.replyTo = replyTo;
    m.ts = nowMillis();
    m.orderKey = m.ts;
    m.status = DeliveryStatus::Sending;
    m.id = store_.append(m);
    statusById_[m.id] = DeliveryStatus::Sending;
    showInActiveView(m, true);
    contacts_.touch(activePeer_, {}, text, m.ts, false);
    beginOperation(QStringLiteral("send:") + QString::number(m.id), QStringLiteral("send"),
        QStringLiteral("To ") + peerName(activePeer_), QStringLiteral("Sending…"), activePeer_);
    emit requestSendText(activePeer_, text, m.id, m.protocolId, replyTo);
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
    const QString replyTo = replying_ ? replyingProtocolId_ : QString();
    if (replying_) {
        cancelReply();
    }
    StoredMessage m;
    m.peer = activePeer_;
    m.outgoing = true;
    m.type = "file";
    m.protocolId = SessionController_genProtocolId();
    m.replyTo = replyTo;
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
    showInActiveView(m, true);
    contacts_.touch(activePeer_, {}, "[file] " + m.attName, m.ts, false);
    beginOperation(QStringLiteral("send:") + QString::number(m.id), QStringLiteral("file-up"),
        m.attName, QStringLiteral("Sending…"), activePeer_);
    emit requestSendFile(activePeer_, localPath, m.id, m.protocolId, replyTo);
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
    if (replying_) {
        cancelReply();  // editing and replying are mutually exclusive composer modes
    }
    editing_ = true;
    editingLocalId_ = localId;
    editingProtocolId_ = protocolId;
    editingText_ = text;
    emit editingChanged();
}

void SessionController::beginReply(
    const QString& protocolId, const QString& previewText, const QString& sender)
{
    if (protocolId.isEmpty()) {
        return;
    }
    if (editing_) {
        cancelEdit();  // mutually exclusive composer modes
    }
    replying_ = true;
    replyingProtocolId_ = protocolId;
    replyingText_ = previewText;
    replyingSender_ = sender;
    emit replyingChanged();
}

void SessionController::cancelReply()
{
    if (!replying_) {
        return;
    }
    replying_ = false;
    replyingProtocolId_.clear();
    replyingText_.clear();
    replyingSender_.clear();
    emit replyingChanged();
}

QVariantMap SessionController::replyPreview(const QString& protocolId) const
{
    QVariantMap info;
    info[QStringLiteral("found")] = false;
    info[QStringLiteral("localId")] = 0;
    info[QStringLiteral("text")] = QString();
    info[QStringLiteral("sender")] = QString();
    if (protocolId.isEmpty() || activePeer_.isEmpty()) {
        return info;
    }
    const StoredMessage m = store_.messageByProtocol(protocolId, activePeer_);
    if (m.id == 0) {
        return info;  // the original is not in our local history: a dead reference
    }
    info[QStringLiteral("found")] = true;
    info[QStringLiteral("localId")] = m.id;
    // A short preview: the text, or a file label for an attachment.
    QString preview = m.text;
    if (preview.isEmpty() && !m.attName.isEmpty()) {
        preview = QStringLiteral("\xF0\x9F\x93\x8E ") + m.attName;  // paperclip + name
    }
    info[QStringLiteral("text")] = preview;
    // The author label: "You" for our own, else a contact/self name or short fp.
    if (m.outgoing) {
        info[QStringLiteral("sender")] = QStringLiteral("You");
    } else {
        info[QStringLiteral("sender")] = peerName(activePeer_);
    }
    return info;
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
    // Remove our own copy with no trace.
    store_.removeById(localId);
    conversation_.removeById(localId);
    statusById_.remove(localId);
    // Refresh the chat-list preview to whatever the new last message now is.
    contacts_.touch(activePeer_, {}, store_.lastText(activePeer_), store_.lastTime(activePeer_),
        false);
    // Ask the recipient to delete it too, but only for our own message: a peer cannot
    // be told to drop a message we received from them.
    if (outgoing && !protocolId.isEmpty()) {
        emit requestSendDelete(activePeer_, protocolId);
    }
}

void SessionController::copyText(const QString& text) const
{
    if (QClipboard* const clipboard = QGuiApplication::clipboard()) {
        clipboard->setText(text);
    }
}

QString SessionController::inviteProblem(const QString& uri) const
{
    const QString trimmed = uri.trimmed();
    if (trimmed.isEmpty()) {
        return QStringLiteral("Paste an invite link.");
    }
    try {
        const bazarish::Descriptor descriptor = bazarish::parseDescriptor(trimmed.toStdString());
        if (descriptor.srv.empty()) {
            return QStringLiteral("This invite carries no address to reach that account.");
        }
    } catch (const std::exception&) {
        // The most common paste by far, and the one that reads as "nothing
        // happened" if it is allowed through: a bare fingerprint.
        static const QRegularExpression fingerprint(QStringLiteral("^[a-z2-7]{52}$"));
        if (fingerprint.match(trimmed).hasMatch()) {
            return QStringLiteral("That is a fingerprint, not an invite. An invite starts with "
                                  "bazarish://invite? and also carries where to reach the account.");
        }
        return QStringLiteral("Not a bazarish://invite link.");
    }
    return {};
}

void SessionController::addByInvite(const QString& uri, const QString& intro)
{
    const QString problem = inviteProblem(uri);
    if (!problem.isEmpty()) {
        emit actionFailed(problem);
        return;  // no background row for something that cannot be attempted
    }
    const QString opId = QStringLiteral("contact:") + SessionController_genProtocolId();
    beginOperation(opId, QStringLiteral("contact"), QStringLiteral("Adding contact"),
        QStringLiteral("Preparing…"));
    // An invite carries who it is for, so the conversation can exist before the
    // request is on its way: the chat opens now and the progress is written into
    // it, instead of a modal parked over the app.
    try {
        const bazarish::Descriptor descriptor
            = bazarish::parseDescriptor(uri.trimmed().toStdString());
        openContactProgress(QString::fromStdString(descriptor.fingerprint), opId,
            QString::fromStdString(descriptor.name));
    } catch (const std::exception& error) {
        // inviteProblem() already vetted the link, so this cannot normally fire;
        // if it ever does, the add still runs and the panel carries the progress.
        bazarish::log::warn("invite parsed for the chat but not for its peer: {}", error.what());
    }
    emit requestAddByInvite(uri, intro, opId);
}

void SessionController::addByUsername(const QString& alias, const QString& intro)
{
    const QString opId = QStringLiteral("contact:") + SessionController_genProtocolId();
    beginOperation(opId, QStringLiteral("contact"), QStringLiteral("Adding ") + alias,
        QStringLiteral("Preparing…"));
    // Who the alias belongs to is only known once the resolver answers, so the
    // chat opens then (onContactRequestSent); until it does, the activity panel
    // is where the progress shows.
    emit requestAddByUsername(alias, intro, opId);
}

void SessionController::openContactProgress(
    const QString& peer, const QString& opId, const QString& name)
{
    if (peer.isEmpty()) {
        return;
    }
    StoredMessage note;
    note.peer = peer;
    note.type = QStringLiteral("system");
    note.text = QStringLiteral("Sending a contact request…");
    note.ts = nowMillis();
    note.orderKey = note.ts;
    note.status = DeliveryStatus::Received;
    note.id = store_.append(note);
    contactProgressRows_[opId] = note.id;
    contacts_.touch(peer, name, note.text, note.ts, false);
    activateConversation(peer);
    showInActiveView(note, true);
}

void SessionController::writeContactProgress(const QString& opId, const QString& text)
{
    const auto found = contactProgressRows_.constFind(opId);
    if (found == contactProgressRows_.cend()) {
        return;
    }
    store_.editContent(found.value(), text, QString());
    conversation_.setTextForId(found.value(), text);
}

void SessionController::acceptContact()
{
    if (activePeer_.isEmpty()) {
        return;
    }
    const QString peer = activePeer_;
    if (acceptingContact_ == peer) {
        return;  // already in flight
    }
    // The button stays where it is and says what it is doing: agreeing is a
    // server round trip, and hiding it on the press made it blink back when the
    // contact list refreshed before the request had finished.
    acceptingContact_ = peer;
    emit acceptingContactChanged();
    emit requestAcceptContact(peer);
}

bool SessionController::contactCanAccept(const QString& fp) const
{
    return pendingContacts_.contains(fp);
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
    const QString& peer, const QString& messageId, const QString& fileUrl)
{
    const QString localPath = QUrl(fileUrl).toLocalFile();
    if (!localPath.isEmpty()) {
        emit requestSaveAttachment(peer, messageId, localPath, 0);
    }
}

void SessionController::saveAttachmentToFile(const QString& peer, const QString& messageId,
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
    beginOperation(QStringLiteral("download:") + QString::number(token), QStringLiteral("file-down"),
        QFileInfo(dest).fileName(), QStringLiteral("Connecting…"), activePeer_);
    emit requestSaveAttachment(peer, messageId, dest, token);
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

void SessionController::setChatFilter(const QString& text)
{
    contactsProxy_.setFilterFixedString(text.trimmed());
}

void SessionController::pinChat(const QString& peer, bool pinned)
{
    if (peer.isEmpty()) {
        return;
    }
    // Local store + immediate re-sort (pinned float to the top), then mirror to the
    // account's other devices so the pinned set stays the same everywhere.
    store_.setPinned(peer, pinned);
    rebuildChatList();
    emit requestSyncChatPin(peer, pinned);
}

bool SessionController::isChatPinned(const QString& peer) const
{
    return store_.isPinned(peer);
}

void SessionController::beginOperation(const QString& id, const QString& kind, const QString& title,
    const QString& status, const QString& peer, const QString& cancelId)
{
    OperationRow row;
    row.id = id;
    row.kind = kind;
    row.title = title;
    row.status = status;
    row.state = eOpRunning;
    row.startedAt = nowMillis();
    row.peer = peer;
    row.cancelId = cancelId;
    operations_.upsert(row);
    emit operationsChanged();
}

void SessionController::updateOperation(
    const QString& id, const QString& status, const QString& detail, double progress)
{
    operations_.update(id, status, detail, progress, eOpRunning);
}

void SessionController::finishOperation(const QString& id, bool ok, const QString& finalStatus)
{
    if (operations_.indexOf(id) < 0) {
        return;
    }
    operations_.update(id, finalStatus, {}, -1.0, ok ? eOpDone : eOpFailed);
    emit operationsChanged();  // the running count just dropped
    // Keep the finished row on screen briefly (longer on failure, so the error is
    // readable), then drop it. Removing a missing id is a no-op, so a row the user
    // already dismissed or that was reused is handled safely.
    QTimer::singleShot(ok ? 3500 : 6000, this, [this, id]() {
        operations_.remove(id);
        emit operationsChanged();
    });
}

void SessionController::generatePersonalKey()
{
    // The worker may be minutes deep in a publish or a slow sync, so the row is
    // opened here, on the GUI thread: the press is acknowledged at once and the
    // worker finishes the same row when it gets to it.
    beginOperation(QStringLiteral("dest-key"), QStringLiteral("dest"),
        QStringLiteral("Creating your destination key"), QStringLiteral("Queued…"));
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

void SessionController::allowClearnet(const bool allow)
{
    clearnetAllowed_ = allow;
    emit requestAllowClearnet(allow);
    emit facadeInfoChanged();
}

void SessionController::publishPersonalDest()
{
    emit requestPublishPersonalDest();
}

void SessionController::disablePersonalDest()
{
    emit requestDisablePersonalDest();
}

void SessionController::refreshI2pStatus()
{
    emit requestRefreshI2pStatus();
}

void SessionController::onI2pKeyState(const bool hasKey, const QString& address)
{
    i2pHasKey_ = hasKey;
    i2pAddress_ = address;
    emit i2pStatusChanged();
}

void SessionController::onI2pStatus(const bool hasKey, const bool delegated, const bool live,
    const QString& address, const QString& summary, const qint64 transientExpires,
    const QString& serverState)
{
    i2pServerState_ = serverState;
    i2pHasKey_ = hasKey;
    i2pEnabled_ = delegated;
    i2pActive_ = live;
    i2pAddress_ = address;
    i2pStatusText_ = summary;
    i2pTransientExpires_ = transientExpires;
    emit i2pStatusChanged();
}

void SessionController::refreshStorageUsage()
{
    emit requestRefreshStorageUsage();
}

void SessionController::onStorageUsageReady(
    const bool mailboxOk, const qulonglong mailboxUsed, const qulonglong mailboxQuota)
{
    // A failed poll leaves the last-known figures and the previous "updated N
    // ago" standing, so a momentarily offline server does not blank the view.
    if (!mailboxOk) {
        return;
    }
    storageMailboxOk_ = true;
    storageMailboxUsed_ = mailboxUsed;
    storageMailboxQuota_ = mailboxQuota;
    storageUpdatedAtMs_ = nowMillis();
    emit storageChanged();
}

QVariantMap SessionController::storageInfo() const
{
    QVariantMap m;
    m[QStringLiteral("mailboxOk")] = storageMailboxOk_;
    m[QStringLiteral("mailboxUsed")] = static_cast<qulonglong>(storageMailboxUsed_);
    m[QStringLiteral("mailboxQuota")] = static_cast<qulonglong>(storageMailboxQuota_);
    m[QStringLiteral("updatedAt")] = storageUpdatedAtMs_;
    m[QStringLiteral("everFetched")] = (storageUpdatedAtMs_ > 0);
    return m;
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
    // Any outcome ends the connect: success clears the screen's busy state,
    // failure leaves the reason on it instead of a silent button.
    if (connecting_) {
        connecting_ = false;
        connectPhase_.clear();
        connectError_ = connected ? QString() : subscriptionText;
        finishOperation(kConnectOperationId, connected,
            connected ? QStringLiteral("Connected") : subscriptionText);
        emit connectStateChanged();
    }
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

void SessionController::rebuildI2pLinks()
{
    emit requestRebuildI2p();
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

void SessionController::onSyncReachable(const bool ok, const QString& reason)
{
    const QString error = ok ? QString() : reason;
    if (reachable_ != ok || syncError_ != error) {
        reachable_ = ok;
        syncError_ = error;
        emit reachableChanged();
    }
}

void SessionController::ackAfterReceive(const QVariantMap& message)
{
    // Runs after onMessageReceived (connected later to the same signal), so the item
    // has already been durably stored/handled. Only now do we ack it on the server,
    // closing the ack-before-store window that lost messages on a restart.
    const QString pendingId = message.value("pendingId").toString();
    if (!pendingId.isEmpty()) {
        emit requestAckPending(pendingId);
    }
}

void SessionController::onMessageReceived(const QVariantMap& message)
{
    const QString peer = message.value("peer").toString();
    const QString type = message.value("type").toString();
    const QString incomingId = message.value("messageId").toString();

    // Idempotent receive, before anything acts on the message. The mailbox is
    // at-least-once: an item whose ack was lost, or that a sender retried, is
    // legitimately re-offered and arrives here again with the same id. Dedup
    // against the transcript - if this conversation already holds an incoming
    // message or note with this id, this is that redelivery. It has to come first:
    // the handlers below return early, and a note stored by one of them (a contact
    // request agreed to, a cleared chat) would otherwise be written once per
    // redelivery. (A read receipt is only sent on a real read, handled by
    // markReadThroughRow.)
    if (!incomingId.isEmpty() && store_.idForIncomingProtocol(incomingId, peer) != 0) {
        return;
    }

    // Call signalling drives the call screen via callStateChanged, never the
    // chat list.
    if (type.startsWith(QStringLiteral("call."))) {
        return;
    }

    // A pin/unpin synced from another of our devices (ref = the pinned chat, text =
    // "1"/"0"): apply it to the local pin list and re-sort. Silent - no bubble.
    if (type == "device.chat-pin") {
        const QString pinPeer = message.value("ref").toString();
        if (!pinPeer.isEmpty()) {
            store_.setPinned(pinPeer, message.value("text").toString() == QStringLiteral("1"));
            rebuildChatList();
        }
        return;
    }

    // A read receipt: the peer read our referenced message (the green state), and
    // by the read high-water everything we sent them before it too. Not shown.
    if (type == "receipt") {
        const QString ref = message.value("ref").toString();
        const qint64 localId = store_.idForProtocol(ref);
        if (localId != 0) {
            markOutgoingRead(peer, localId);
        }
        return;
    }

    // A reaction: record the reactor's emoji against the target message and
    // re-drive the chips. Never a chat bubble. The reactor is the peer who sent it.
    if (type == "reaction") {
        store_.setReaction(peer, message.value("ref").toString(), peer,
            message.value("text").toString());
        ++reactionsRevision_;
        emit reactionsRevisionChanged();
        return;
    }

    // An in-place edit of a message this peer previously sent us: update it
    // where it sits instead of adding a new bubble. Scoped to incoming-from-peer
    // in the store, so a peer can only edit its own messages.
    if (type == "edit") {
        // Scoped to incoming-from-peer in the store, so a peer can only edit its
        // own messages.
        const qint64 localId
            = store_.idForIncomingProtocol(message.value("ref").toString(), peer);
        if (localId != 0) {
            const QString newText = message.value("text").toString();
            const QString newKeyboard = message.value("keyboard").toString();
            store_.editContent(localId, newText, newKeyboard);
            if (peer == activePeer_) {
                conversation_.editById(localId, newText, newKeyboard);
            }
            // If we had already read this message, the edit is read again the moment
            // it lands in the open chat: re-acknowledge it so the sender's edited
            // bubble can advance to delivered (green).
            if (peer == activePeer_ && sendReceipts_
                && localId <= lastReadAckedId_.value(peer, 0)) {
                emit requestSendReceipt(peer, message.value("ref").toString());
            }
            QString preview = newText;
            if (preview.isEmpty() && !newKeyboard.isEmpty()) {
                preview = "[interactive]";
            }
            contacts_.touch(peer, peerName(peer), preview, nowMillis(), false);
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

    // The peer cleared the whole conversation for everyone: honour their wish and
    // wipe our transcript with them, leaving a single note so the empty chat
    // explains itself.
    if (type == "chat.clear") {
        store_.clearPeer(peer);
        StoredMessage sys;
        sys.peer = peer;
        sys.protocolId = incomingId;  // so a redelivery is recognised as one
        sys.type = QStringLiteral("system");
        sys.text = peerName(peer) + QStringLiteral(" cleared the chat.");
        sys.ts = nowMillis();
        sys.orderKey = sys.ts;
        sys.status = DeliveryStatus::Received;
        sys.id = store_.append(sys);
        if (peer == activePeer_) {
            loadLatestWindow();
        }
        contacts_.touch(peer, peerName(peer), sys.text, sys.ts, false);
        contacts_.setUnread(peer, store_.unreadCount(peer));
        refreshUnreadTotal();
        return;
    }

    // The peer agreed to our contact request: we are now mutual contacts (their
    // descriptor already arrived via the bootstrap in sync). Surface a note.
    if (type == "contact.accept") {
        StoredMessage sys;
        sys.peer = peer;
        sys.protocolId = incomingId;  // so a redelivery is recognised as one
        sys.type = QStringLiteral("system");
        sys.text = peerName(peer) + QStringLiteral(" accepted your contact request.");
        sys.ts = nowMillis();
        sys.orderKey = sys.ts;
        sys.status = DeliveryStatus::Received;
        sys.id = store_.append(sys);
        showInActiveView(sys, false);
        contacts_.touch(peer, peerName(peer), sys.text, sys.ts, false);
        contacts_.setUnread(peer, store_.unreadCount(peer));
        return;
    }

    // Control content with nothing to show. A token refill (the peer topping up
    // our capacity to write to their mailbox) carries no text, and neither does a
    // button press or a type a newer client sends that this one cannot render.
    // Stored, each became an empty bubble that also counted as unread. The item is
    // still acked - ackAfterReceive runs off the same signal - so it does not come
    // back.
    if (message.value("text").toString().isEmpty()
        && message.value("attName").toString().isEmpty()
        && message.value("attRef").toString().isEmpty()
        && message.value("keyboard").toString().isEmpty()) {
        bazarish::log::info("silent control message ({}) not shown", type.toStdString());
        return;
    }

    StoredMessage m;
    m.peer = peer;
    // Another device of ours sent this; it belongs on our side of the chat.
    m.outgoing = message.value("sentByUs").toBool();
    m.type = type;
    m.protocolId = message.value("messageId").toString();
    m.text = message.value("text").toString();
    m.replyTo = message.value("replyTo").toString();
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
    // An echo carries no delivery state of its own: the device that sent it owns
    // that, and a receipt from the contact will still arrive here.
    m.status = m.outgoing ? DeliveryStatus::AtSenderServer : DeliveryStatus::Received;
    m.id = store_.append(m);

    showInActiveView(m, false);
    QString preview = m.text;
    if (preview.isEmpty() && !m.attName.isEmpty()) {
        preview = "[" + type + "] " + m.attName;
    }
    contacts_.touch(peer, peerName(peer), preview, m.ts, false);
    // The unread badge is the persistent count of incoming messages past the read
    // high-water (set when messages actually scroll into view), not a running
    // increment - so it stays accurate across restarts and partial reads.
    contacts_.setUnread(peer, store_.unreadCount(peer));
    // No receipt is sent on arrival: the green "read" state is reported only when
    // the user actually reads the message (chat open + window focused + the message
    // in view), driven by markReadThroughRow.
}

void SessionController::onAvatarReady(const QString& fingerprint, const QByteArray& data)
{
    AvatarStore::instance().put(fingerprint, data);
    if (fingerprint == fingerprint_) {
        avatarBusy_ = false;
        emit avatarChanged();
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
    const QString opId = QStringLiteral("send:") + QString::number(localId);
    if (state == DeliveryStatus::AtSenderServer) {
        // The registry follows the handover, not the journey: once our own server
        // holds the envelope the row is done. Delivery and reading show up on the
        // message itself, and a failure comes back from our server as its status.
        finishOperation(opId, true, QStringLiteral("Accepted by your server"));
    }
}

void SessionController::onUploadProgress(qint64 localId, qint64 sent, qint64 total)
{
    const double fraction = total > 0 ? static_cast<double>(sent) / static_cast<double>(total) : 0.0;
    conversation_.setUploadProgressForId(localId, fraction);
    updateOperation(QStringLiteral("send:") + QString::number(localId), QStringLiteral("Uploading…"),
        humanBytes(sent) + QStringLiteral(" / ") + humanBytes(total), fraction);
}

void SessionController::onDownloadProgress(qint64 token, qint64 received, qint64 total)
{
    conversation_.setDownloadProgressForId(token, received, total);
    const double fraction
        = total > 0 ? static_cast<double>(received) / static_cast<double>(total) : -1.0;
    updateOperation(QStringLiteral("download:") + QString::number(token),
        QStringLiteral("Downloading…"),
        total > 0 ? humanBytes(received) + QStringLiteral(" / ") + humanBytes(total) : QString(),
        fraction);
}

void SessionController::onTransferStage(
    const QString& peer, const QString& protocolId, const QString& stage)
{
    TransferProgress& progress = transfers_[protocolId];
    progress.peer = peer;
    progress.stage = stage;
    const StoredMessage m = store_.messageByProtocol(protocolId, peer);
    if (m.id == 0) {
        return;
    }
    // A transfer runs long and can be stopped, so it gets a row of its own the
    // moment it starts - a send row from an hour ago is not that row.
    const QString opId = (m.outgoing ? QStringLiteral("send:") : QStringLiteral("download:"))
        + QString::number(m.id);
    if (operations_.indexOf(opId) < 0) {
        beginOperation(opId, m.outgoing ? QStringLiteral("file-up") : QStringLiteral("file-down"),
            m.attName.isEmpty() ? QStringLiteral("file") : m.attName, stage, peer, protocolId);
    }
    // The row exists whether or not this conversation is on screen; the model
    // only has it while it is, and replayTransfersForActivePeer puts it back.
    if (peer == activePeer_) {
        conversation_.setTransferStageForId(m.id, stage);
    }
    updateOperation((m.outgoing ? QStringLiteral("send:") : QStringLiteral("download:"))
            + QString::number(m.id),
        stage);
}

void SessionController::onServedProgress(
    const QString& peer, const QString& protocolId, qint64 sent, qint64 total)
{
    TransferProgress& progress = transfers_[protocolId];
    progress.peer = peer;
    progress.sent = sent;
    progress.total = total;
    const StoredMessage m = store_.messageByProtocol(protocolId, peer);
    if (m.id == 0) {
        return;
    }
    const double fraction
        = total > 0 ? static_cast<double>(sent) / static_cast<double>(total) : -1.0;
    if (peer == activePeer_) {
        if (m.outgoing) {
            conversation_.setUploadProgressForId(m.id, fraction);
        } else {
            conversation_.setDownloadProgressForId(m.id, sent, total);
        }
    }
    updateOperation((m.outgoing ? QStringLiteral("send:") : QStringLiteral("download:"))
            + QString::number(m.id),
        progress.stage,
        total > 0 ? humanBytes(sent) + QStringLiteral(" / ") + humanBytes(total) : QString(),
        fraction);
}

void SessionController::onServedFinished(
    const QString& peer, const QString& protocolId, const bool ok, const QString& error)
{
    const StoredMessage m = store_.messageByProtocol(protocolId, peer);
    if (m.id == 0 || peer != activePeer_) {
        // Nobody is looking at this conversation: remember the outcome so opening
        // it shows what happened, instead of a bubble that quietly lost its bar.
        TransferProgress& kept = transfers_[protocolId];
        kept.peer = peer;
        kept.stage.clear();
        kept.finished = true;
        kept.ok = ok;
        kept.error = error;
        if (!ok) {
            emit actionFailed(error);  // and say it now, wherever the user is
        }
        if (m.id != 0) {
            finishOperation((m.outgoing ? QStringLiteral("send:") : QStringLiteral("download:"))
                    + QString::number(m.id),
                ok, ok ? QStringLiteral("Transferred") : error);
        }
        return;
    }
    transfers_.remove(protocolId);
    if (!m.outgoing) {
        // An incoming transfer ends where a download ends: the saved path, the
        // bubble's own state and its row, all keyed by the message id.
        conversation_.setTransferStageForId(m.id, {});
        onDownloadFinished(m.id, ok, error);
        return;
    }
    if (peer == activePeer_) {
        conversation_.setUploadProgressForId(m.id, -1.0);
        conversation_.setTransferStageForId(m.id, {});
        if (!ok) {
            conversation_.setErrorForId(m.id, error);
        }
    }
    finishOperation((m.outgoing ? QStringLiteral("send:") : QStringLiteral("download:"))
            + QString::number(m.id),
        ok, ok ? QStringLiteral("Transferred") : error);
}

void SessionController::cancelTransfer(const QString& protocolId)
{
    emit requestCancelTransfer(protocolId);
}

void SessionController::replayTransfersForActivePeer()
{
    QStringList settled;
    for (auto it = transfers_.constBegin(); it != transfers_.constEnd(); ++it) {
        if (it.value().peer != activePeer_) {
            continue;
        }
        const StoredMessage m = store_.messageByProtocol(it.key(), activePeer_);
        if (m.id == 0) {
            continue;
        }
        if (it.value().finished) {
            // The outcome arrived while this conversation was closed.
            conversation_.setTransferStageForId(m.id, {});
            if (m.outgoing) {
                conversation_.setUploadProgressForId(m.id, -1.0);
                if (!it.value().ok) {
                    conversation_.setErrorForId(m.id, it.value().error);
                }
            } else {
                conversation_.finishDownloadForId(m.id, it.value().ok, it.value().error);
            }
            settled << it.key();
            continue;
        }
        conversation_.setTransferStageForId(m.id, it.value().stage);
        if (it.value().total > 0) {
            if (m.outgoing) {
                conversation_.setUploadProgressForId(m.id,
                    static_cast<double>(it.value().sent) / static_cast<double>(it.value().total));
            } else {
                conversation_.setDownloadProgressForId(m.id, it.value().sent, it.value().total);
            }
        }
    }
    for (const QString& id : settled) {
        transfers_.remove(id);
    }
}

void SessionController::onDownloadStage(qint64 token, int stage)
{
    conversation_.setDownloadStageForId(token, stage);
    // BlobFetchStage: 0 connecting, 1 downloading, 2 reconnecting.
    static const char* const kStages[] = {"Connecting…", "Downloading…", "Reconnecting…"};
    if (stage >= 0 && stage <= 2) {
        updateOperation(QStringLiteral("download:") + QString::number(token),
            QString::fromLatin1(kStages[stage]));
    }
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
    const QString opId = QStringLiteral("download:") + QString::number(token);
    if (notFound) {
        store_.setBlobGone(token, true);
        conversation_.setBlobGoneForId(token, true);
        conversation_.finishDownloadForId(token, false, QString());
        finishOperation(opId, false, QStringLiteral("File no longer available"));
        return;
    }
    finishOperation(opId, ok, ok ? QStringLiteral("Saved") : (QStringLiteral("Failed: ") + error));
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
    finishOperation(QStringLiteral("send:") + QString::number(localId), false,
        QStringLiteral("Failed: ") + error);
}

void SessionController::onSendPhase(qint64 localId, const QString& phase)
{
    // Real server-reported delivery phase for a still-in-flight send, shown on its
    // activity row (a no-op if the row already settled).
    updateOperation(QStringLiteral("send:") + QString::number(localId), humanFederationPhase(phase));
}

void SessionController::onSendSettled(qint64 localId, const QString& note)
{
    // The send left our server but its delivery was never confirmed (retries
    // exhausted or the attempt was forgotten). Settle the activity row as a
    // non-failure: the message bubble stays grey and may still turn green on a
    // later read receipt.
    finishOperation(QStringLiteral("send:") + QString::number(localId), true,
        note == QStringLiteral("unconfirmed") ? QStringLiteral("Sent — delivery unconfirmed")
                                              : QStringLiteral("Sent"));
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
    // Preserve the original reply reference on a resend.
    const QString replyTo = store_.messageByProtocol(protocolId, activePeer_).replyTo;
    // A resend is a send: it travels the same way and takes the same time, so it
    // belongs in the activity panel like the first attempt did.
    beginOperation(QStringLiteral("send:") + QString::number(localId), QStringLiteral("send"),
        QStringLiteral("To ") + peerName(activePeer_), QStringLiteral("Sending again…"),
        activePeer_);
    emit requestSendText(activePeer_, text, localId, protocolId, replyTo);
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
    const QString replyTo = store_.messageByProtocol(protocolId, activePeer_).replyTo;
    const StoredMessage stored = store_.messageByProtocol(protocolId, activePeer_);
    beginOperation(QStringLiteral("send:") + QString::number(localId), QStringLiteral("file-up"),
        stored.attName.isEmpty() ? QStringLiteral("file") : stored.attName,
        QStringLiteral("Sending again…"), activePeer_);
    emit requestSendFile(activePeer_, srcPath, localId, protocolId, replyTo);
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

QStringList SessionController::standardReactions() const
{
    return kStandardReactions;
}

void SessionController::rememberReaction(const QString& emoji)
{
    const QString trimmed = emoji.trimmed();
    if (trimmed.isEmpty()) {
        return;
    }
    recentReactions_.removeAll(trimmed);
    recentReactions_.prepend(trimmed);
    while (recentReactions_.size() > kRecentReactions) {
        recentReactions_.removeLast();
    }
    QJsonArray array;
    for (const QString& entry : recentReactions_) {
        array.append(entry);
    }
    const QByteArray text = QJsonDocument(array).toJson(QJsonDocument::Compact);
    profileDb().putText("recent-reactions", text.toStdString());
    emit recentReactionsChanged();
}

void SessionController::react(const QString& protocolId, const QString& emoji)
{
    if (activePeer_.isEmpty() || protocolId.isEmpty()) {
        return;
    }
    // Toggle: tapping the emoji we already set removes our reaction.
    const QString next = (myReaction(protocolId) == emoji) ? QString() : emoji;
    // Setting one they reached for outside the standard set - typed, or tapped on
    // somebody else's chip - puts it in their recents. Removing one does not.
    if (!next.isEmpty() && !kStandardReactions.contains(next)) {
        rememberReaction(next);
    }
    store_.setReaction(activePeer_, protocolId, fingerprint_, next);
    emit requestSendReaction(activePeer_, protocolId, next);
    ++reactionsRevision_;
    emit reactionsRevisionChanged();
}

QString SessionController::myReaction(const QString& protocolId) const
{
    for (const Reaction& r : store_.reactionsFor(activePeer_, protocolId)) {
        if (r.reactor == fingerprint_) {
            return r.emoji;
        }
    }
    return {};
}

QVariantList SessionController::reactionSummary(const QString& protocolId) const
{
    QVariantList out;
    if (activePeer_.isEmpty() || protocolId.isEmpty()) {
        return out;
    }
    // Aggregate by emoji, preserving the order each emoji was first seen.
    QStringList order;
    QHash<QString, int> counts;
    QString mine;
    for (const Reaction& r : store_.reactionsFor(activePeer_, protocolId)) {
        if (!counts.contains(r.emoji)) {
            order << r.emoji;
        }
        ++counts[r.emoji];
        if (r.reactor == fingerprint_) {
            mine = r.emoji;
        }
    }
    for (const QString& e : order) {
        QVariantMap m;
        m[QStringLiteral("emoji")] = e;
        m[QStringLiteral("count")] = counts.value(e);
        m[QStringLiteral("mine")] = (e == mine);
        out << m;
    }
    return out;
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
    // Persist the read high-water and refresh the unread badge: the count drops as
    // messages genuinely scroll into the focused viewport. Monotonic, so re-reading
    // older history never lowers it.
    store_.setLastReadId(activePeer_, id);
    contacts_.setUnread(activePeer_, store_.unreadCount(activePeer_));
    const qint64 prevAcked = lastReadAckedId_.value(activePeer_, 0);
    if (id <= prevAcked) {
        return;  // already acknowledged up to here
    }
    lastReadAckedId_[activePeer_] = id;
    // Sending a read receipt is opt-in (the "send read receipts" setting). The
    // unread high-water above is advanced regardless, so unread tracking always
    // works even with receipts disabled.
    if (!sendReceipts_) {
        return;
    }
    // A read sends a delivery receipt so the sender's bubble greens.
    emit requestSendReceipt(activePeer_, protocolId);
}

void SessionController::onContactAddStage(const QString& opId, const QString& status)
{
    updateOperation(opId, status);
    writeContactProgress(opId, status);
}

void SessionController::onOpBegin(
    const QString& opId, const QString& kind, const QString& title, const QString& status)
{
    beginOperation(opId, kind, title, status);
}

void SessionController::onOpDone(const QString& opId, bool ok, const QString& status)
{
    finishOperation(opId, ok, status);
}

void SessionController::onContactAccepted(const QString& peer, const bool ok,
    const QString& reason)
{
    if (acceptingContact_ == peer) {
        acceptingContact_.clear();
        emit acceptingContactChanged();
    }
    if (!ok) {
        bazarish::log::warn("agreeing to {} failed: {}", peer.toStdString(), reason.toStdString());
        return;  // the button comes back enabled; the failure is on screen already
    }
    pendingContacts_.remove(peer);
    ++contactsRevision_;
    emit contactsRevisionChanged();
}

void SessionController::onContactAddDone(const QString& opId, bool ok, const QString& status)
{
    finishOperation(opId, ok, status);
    // The note in the chat carries the outcome and then stops being a progress
    // line: a failed add says why, right where the user was watching.
    writeContactProgress(opId, ok ? status : QStringLiteral("Could not add: ") + status);
    contactProgressRows_.remove(opId);
}

void SessionController::onContactRequestSent(const QString& fingerprint, const QString& intro)
{
    // An add by alias only learns who the peer is here, so this is where its chat
    // opens; an add by invite opened one already and just carries on in it.
    if (!fingerprint.isEmpty() && activePeer_ != fingerprint) {
        activateConversation(fingerprint);
    }
    // Mirror the request on our own side: store the intro we just sent as an
    // outgoing message and open a chat for the new peer, so adding a contact
    // produces a visible conversation immediately instead of an empty chat-list
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
    emit requestStartCall(target);
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

void SessionController::onCallStateChanged(const int state, const QString& peer,
    const QString& callId, const bool muted, const QString& stage, const qint64 connectedAtMs)
{
    static const char* const kNames[] = {"idle", "outgoing", "incoming", "active"};
    const QString name = (state >= 0 && state <= 3) ? QString::fromLatin1(kNames[state])
                                                    : QStringLiteral("idle");
    if (callState_ == name && callPeer_ == peer && callId_ == callId && callMuted_ == muted
        && callStage_ == stage && callConnectedAtMs_ == connectedAtMs) {
        return;
    }
    callStage_ = stage;
    callConnectedAtMs_ = connectedAtMs;
    callState_ = name;
    callPeer_ = peer;
    callId_ = callId;
    callMuted_ = muted;
    emit callChanged();

    // Surface the call as a background operation: it begins on an outgoing/incoming
    // invite and the active leg, and finishes when the call returns to idle.
    if (state == 0) {  // idle: the call (if any) ended
        if (!callOpId_.isEmpty()) {
            finishOperation(callOpId_, true, QStringLiteral("Call ended"));
            callOpId_.clear();
        }
    } else {
        const QString opId = QStringLiteral("call:") + (callId.isEmpty() ? peer : callId);
        const QString title = QStringLiteral("Call with ") + peerName(peer);
        const QString status = state == 1 ? QStringLiteral("Calling…")
            : state == 2                  ? QStringLiteral("Incoming call…")
                                          : QStringLiteral("Connected");
        if (callOpId_ != opId) {
            callOpId_ = opId;
            beginOperation(opId, QStringLiteral("call"), title, status, peer);
        } else {
            updateOperation(opId, status);
        }
    }
}

void SessionController::onCallLogged(
    const QString& peer, bool incoming, int outcome, qint64 durationSec)
{
    if (peer.isEmpty()) {
        return;
    }
    // Outcome ints mirror Session::CallOutcome: 0 answered, 1 no-answer, 2 declined,
    // 3 missed, 4 cancelled, 5 busy.
    const QString dir
        = incoming ? QStringLiteral("Incoming call") : QStringLiteral("Outgoing call");
    QString text;
    switch (outcome) {
    case 0:
        text = dir + QStringLiteral(", ") + QString::number(durationSec / 60)
            + QStringLiteral(":")
            + QString::number(durationSec % 60).rightJustified(2, QLatin1Char('0'));
        break;
    case 1: text = QStringLiteral("Outgoing call, no answer"); break;
    case 2: text = dir + QStringLiteral(", declined"); break;
    case 3: text = QStringLiteral("Missed call"); break;
    case 4: text = QStringLiteral("Outgoing call, cancelled"); break;
    case 5: text = QStringLiteral("Outgoing call, busy"); break;
    default: text = dir; break;
    }

    StoredMessage sys;
    sys.peer = peer;
    sys.type = QStringLiteral("system");
    sys.text = text;
    sys.ts = nowMillis();
    sys.orderKey = sys.ts;
    sys.status = DeliveryStatus::Received;
    sys.id = store_.append(sys);
    showInActiveView(sys, false);
    contacts_.touch(peer, peerName(peer), text, sys.ts, false);
    contacts_.setUnread(peer, store_.unreadCount(peer));
    refreshUnreadTotal();
}

}  // namespace bazarish::app
