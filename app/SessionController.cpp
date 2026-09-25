// Bazarish project (c) 2026
#include "SessionController.hpp"

#include <QFile>

#include <QJsonDocument>

#include <QJsonArray>

#include "I2pRouter.hpp"

#include "AvatarStore.hpp"
#include "PictureStore.hpp"
#include "DeliveryStatus.hpp"
#include "FederationFetch.hpp"
#include "Invite.hpp"
#include "QtAudioIo.hpp"
#include "Session.hpp"

#include <bazarish/Crypto.hpp>
#include <bazarish/Limits.hpp>
#include <bazarish/Descriptor.hpp>
#include <bazarish/Portal.hpp>

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
#include <QMetaMethod>
#include <QSet>
#include <QImage>
#include <QMimeDatabase>
#include <QRandomGenerator>
#include <QStandardPaths>
#include <QRegularExpression>
#include <chrono>
#include <QTimer>
#include <cstring>
#include <QUrl>

#if defined(Q_OS_LINUX) && defined(BAZARISH_HAVE_QTDBUS)
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>
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
    // What the resolve is doing while it runs, in the order it said it. Held
    // here rather than emitted from the resolve thread for the reason the
    // results are: that thread must touch nothing that can be destroyed under
    // it.
    std::vector<std::pair<QString, QString>> stages;
};

struct AliasErrandQueue {
    std::mutex mutex;
    std::vector<bazarish::client::Session::AliasErrandResult> results;
};

using bazarish::client::IncomingMessage;
using bazarish::client::ServerEndpoint;
using bazarish::client::Session;

namespace {
// Length of the "://" that separates a URL scheme from its authority.
constexpr int kSchemeSeparatorLength = 3;
// A contact request names itself with this many random bytes, and keeps the name
// if the add has to be taken up again.
constexpr std::size_t kContactRequestIdBytes = 8;

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

// A pass on the worker thread holds every command the user asks for behind it,
// so one that runs longer than this says so with its name. It is not a fault -
// a mailbox pass is a round trip and takes what it takes - it is the only way to
// know which pass is the one a person is waiting out.
constexpr std::chrono::milliseconds kSlowPass{400};
// A piece of one of those passes: smaller, so the piece that is the pass can be
// told from the pieces that are not.
constexpr std::chrono::milliseconds kSlowStretch{200};

// How often the queue of commands is looked over, and how long a command has to
// be unfinished before it is worth a row: a local one is done in a moment, and
// a panel that flickers with those is a panel nobody reads.
constexpr int kCommandTickMs = 200;
constexpr qint64 kCommandVisibleAfterMs = 400;

// Commands that raise a row of their own, with more in it than a generic one
// has: what is being sent, to whom, how far it has got. A second row for them
// would say less and be in the way. Everything not named here is covered by
// the generic rows, which is what keeps new commands from having to be
// remembered one by one.
const QSet<QByteArray> kSelfDescribingCommands = {
    "requestOpen", "requestConnect", "requestShutdown", "requestSendText", "requestSendFile",
    "requestSendPicture", "requestSendVoice", "requestSendCallback", "requestSendCommand",
    "requestAddByInvite", "requestAddByAlias", "requestAcceptContact", "requestInviteSig",
    "requestExport", "requestSaveAttachment", "requestActivateAliasServicing",
    "requestPublishThisDeviceAddress", "requestPublishFreshAddress", "requestRefreshI2pStatus",
    "requestRefreshStorageUsage", "requestRefreshDevices", "requestForgetDevice",
    "requestCloseAccountOnServer", "requestGeneratePersonalKey", "requestLoadPersonalKey",
    "requestDeletePersonalKey", "requestPublishPersonalDest",
};

// Keeping this account's delegation alive. The transient the server operates the
// destination with lasts 7 days, so it is re-issued about 2 days early, with a
// few hours of per-device jitter so several devices do not all issue at once
// (the poll-before-issue inside stands the losers down). Checked at most hourly:
// the check itself is a server call, and the sync tick is seconds.
constexpr qint64 kTransientCheckIntervalMs = 3600 * 1000;
// An account held for approval asks again on this cadence: often enough that the
// user is not left staring at a stale warning after the operator lets them in,
// rare enough to be one small request a minute.
constexpr qint64 kApprovalCheckIntervalMs = 60 * 1000;
// A device the server does not know cannot be told anything, so registering it
// is retried from the sync tick - but no faster than this: the registration is
// several calls, and the reason it failed is usually not a passing one.
constexpr qint64 kRegisterRetryIntervalMs = 60 * 1000;
// Keeping the alias registry pointed at this account is two I2P round trips at
// worst and usually none at all, but a registry that is down would otherwise be
// dialled on every sync tick. A minute is soon enough after a destination moves,
// and the status ask behind it has a window of its own that is far longer.
constexpr qint64 kAliasServiceIntervalMs = 60 * 1000;
constexpr qint64 kTransientRenewLeadSeconds = 5 * 24 * 3600;
constexpr qint64 kTransientJitterSeconds = 6 * 3600;
// A voice message rides inside one message, so what really bounds it is the
// payload cap, not the clock: recording stops once the encoded audio has spent
// its share. The reserve covers the message around it (ids, reply, the CBOR
// keys), which is far smaller than this but must not be cut fine.
constexpr qint64 kMaxVoiceMs = 2 * 60 * 1000;
constexpr std::size_t kVoiceEnvelopeReserveBytes = 8 * 1024;
constexpr std::size_t kMaxVoiceBytes
    = bazarish::kMaxMessagePayloadBytes - kVoiceEnvelopeReserveBytes;
// Below this it is a slip of the finger, not a message.
constexpr qint64 kMinVoiceMs = 700;
// How often the recording clock and the input level are reported to the UI: the
// level is a live picture of the microphone, so it is sampled at a rate a user
// reads as movement rather than as steps.
constexpr int kVoiceTickMs = 50;
// How often a live call is asked for its state. The moment media starts flowing
// is what both sides show as "in call", and the mailbox poll is far too coarse
// to carry it; the level meters are drawn from the same tick, which is what sets
// the rate - slower than this and a voice reads as a series of steps.
constexpr int kCallWatchIntervalMs = 100;

// A contact request the recipient's address refused for being over its cap is
// sent again on a timer: enough tries to ride out a busy minute, spaced so the
// next one lands in a fresh window.
constexpr int kContactRetryAttempts = 3;
constexpr int kContactRetrySeconds = 20;
constexpr int kMillisecondsPerSecond = 1000;
// How many bars a voice message's drawn waveform has - enough shape to read at
// the width of a bubble.
constexpr int kVoiceWaveBars = 40;
// The speeds a voice message plays back at, stepped through by the bubble's own
// control. Faster playback raises the pitch with it: the samples are handed to
// the device faster, and nothing time-stretches them back.
constexpr std::array<double, 3> kVoiceSpeeds = {1.0, 1.5, 2.0};

// A voice message's drawn shape, as one hex digit a bar (kWaveformLevels is 16,
// so a level is exactly a digit). Empty when the audio will not unpack - the
// bubble then draws no waveform rather than a made-up one.
QString waveformHex(const Bytes& opus)
{
    std::vector<std::uint8_t> bars;
    try {
        bars = voiceWaveform(opus, kVoiceWaveBars);
    } catch (const std::exception& error) {
        bazarish::log::warn("voice waveform: {}", error.what());
        return {};
    }
    QString hex;
    hex.reserve(static_cast<qsizetype>(bars.size()));
    for (const std::uint8_t bar : bars) {
        hex.append(QChar::fromLatin1("0123456789abcdef"[bar]));
    }
    return hex;
}

// How long the server is asked to hold the mail request open. Its own cap is
// lower; asking for more than it allows is answered sooner, which costs nothing.
// The request is answered the moment something lands, so this is only how often
// the loop asks again while nothing does.
constexpr int kEventWaitSeconds = 30;
// How often the local upkeep runs. It reads no mail: mail arrives by the wait
// above and by nothing else.
constexpr int kMaintenanceIntervalMs = 2000;

// The background-activity row for a connect: the user can hide the progress
// dialog and still watch the connect finish in the activity panel.
const QString kConnectOperationId = QStringLiteral("connect");

// One row per alias for the invite sheet: the name, and the day it runs out.
// Only names their owner pointed at this identity are listed - an alias that
// points nowhere reaches nobody, so offering it as a way to be reached would be
// handing out something that does not work.
QVariantList aliasHoldingRows(const std::vector<bazarish::client::Session::AliasHolding>& held)
{
    QVariantList rows;
    for (const bazarish::client::Session::AliasHolding& holding : held) {
        if (!holding.bindingWanted) {
            continue;
        }
        QVariantMap row;
        row[QStringLiteral("alias")] = QString::fromStdString(holding.alias);
        row[QStringLiteral("term")]
            = (holding.autoRenew ? QStringLiteral("renews ") : QStringLiteral("expires "))
            + QDateTime::fromSecsSinceEpoch(holding.notAfter).date().toString(
                QStringLiteral("yyyy-MM-dd"));
        rows << row;
    }
    return rows;
}

// What is said under that table, when anything needs saying at all. The figure
// behind the deposit stays on the service; what a person needs here is that the
// alias will lapse unless they do something.
QString aliasHoldingsNote(const QVariantList& rows, const bool depositCovers)
{
    if (rows.isEmpty() || depositCovers) {
        return QString();
    }
    return QStringLiteral("Your deposit will not cover the next renewal.");
}

// The alias errand is two round trips over I2P and can take a while; it belongs
// in the activity panel with everything else that reaches the network, not
// behind a button that quietly greys out.
const QString kAliasOperationId = QStringLiteral("alias");
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

// A send is retrying. The courier writes "retry <n>/<attempts>", so the prefix
// is what names the phase and the rest is the count.
bool isRetryPhase(const QString& phase)
{
    return phase.startsWith(QLatin1StringView(bazarish::client::kPhaseRetryPrefix));
}

// Maps a delivery phase reported by the courier to a human-readable activity
// status. The phase words are the transport's, so they are taken from it.
QString humanDeliveryPhase(const QString& phase)
{
    if (phase == QLatin1StringView(bazarish::client::kPhasePreparing)) {
        // Making the one-time address this correspondent's mail leaves from, and
        // waiting for its tunnels when it had to be built cold.
        return QStringLiteral("Preparing an address to send from…");
    }
    if (phase == QLatin1StringView(bazarish::client::kPhaseDialing)) {
        return QStringLiteral("Reaching the recipient's server…");
    }
    if (phase == QLatin1StringView(bazarish::client::kPhaseSending)) {
        return QStringLiteral("Sending over I2P…");
    }
    if (isRetryPhase(phase)) {
        const QLatin1StringView prefix(bazarish::client::kPhaseRetryPrefix);
        const QStringList parts = phase.sliced(prefix.size()).trimmed().split(QChar('/'));
        if (parts.size() == 2) {
            return QStringLiteral("Trying again (") + parts.at(0) + QStringLiteral(" of ")
                + parts.at(1) + QStringLiteral(")…");
        }
    }
    return phase;
}

// How a send reports itself back to the model. Both callbacks arrive on a
// delivery worker thread, so they only emit: the signals are queued onto the
// thread that owns the conversation.
bazarish::client::DeliveryWatch watchFor(SessionWorker* const worker, const qint64 localId)
{
    bazarish::client::DeliveryWatch watch;
    watch.onPhase = [worker, localId](const std::string& phase) {
        if (phase == bazarish::client::kPhaseDialing) {
            // Past the local part: from here the message is on its way to the
            // recipient's server, which is what the solid grey chip says.
            emit worker->sendProgress(localId, DeliveryStatus::Delivering);
        }
        emit worker->sendPhase(localId, QString::fromStdString(phase));
    };
    watch.onOutcome
        = [worker, localId](const bazarish::client::OutboundCourier::Outcome& outcome) {
              // A send can settle a contact - a first reply is what establishes
              // one - so what the account window shows about it may be out of
              // date the moment this runs. Asked for on the worker's thread,
              // which is where the session may be read.
              QMetaObject::invokeMethod(worker, "refreshContacts", Qt::QueuedConnection);
              if (outcome.stored) {
                  emit worker->sendProgress(localId, DeliveryStatus::AtRecipientServer);
                  emit worker->sendResult(localId, true, {});
                  return;
              }
              emit worker->sendResult(localId, false,
                  QString::fromStdString(outcome.errorMessage.empty()
                          ? std::string("the recipient's server could not be reached")
                          : outcome.errorMessage));
          };
    return watch;
}

// When a received message was written, and where it therefore sits. Both are the
// sender's own sentAt, taken from inside the sealed envelope: nothing a server
// says about a message decides where it goes, because the server is not told
// anything about the message to say (docs-main Messages.md "Ordering and
// timestamps"). The local clock stands in only for an envelope that carries no
// time at all, which nothing this client sends does.
struct Placement {
    qint64 displayTs = 0;
    qint64 orderKey = 0;
};
Placement placeReceived(const qint64 sentAtMs, const qint64 arrivalMs)
{
    const qint64 written = sentAtMs > 0 ? sentAtMs : arrivalMs;
    return {written, written};
}

// How many messages a conversation loads per page (initial window and each
// older/newer step). Small on purpose: opening a chat should cost what is on
// screen, not what the chat has ever held, and the rest arrives as the user
// scrolls into it.
// One window of a conversation, and one page of older history. It has to be worth
// a screen: paging asks for a screenful of loaded content above the viewport, so a
// page shorter than the screen leaves the condition true and the next scroll loads
// another one - which is how a scroll upwards turned into a page per tick.
constexpr int kPageSize = 50;
// How long the storage window is given to paint "this is running" before the work
// that holds the thread begins. One frame is enough; this is two at 60 Hz.
constexpr int kBusyPaintDelayMs = 32;
// How long reading settles before the account's other devices are told about it.
// Long enough that scrolling through a conversation is one message rather than
// dozens, short enough that closing the lid right after does not lose the mark.
constexpr int kReadSyncIdleMs = 4000;

// How many reactions outside the standard set the picker remembers.
constexpr int kRecentReactions = 5;
// How many unseen reactions are remembered for their flash. A person who has
// been away comes back to a handful of them, not to a list that grew all week;
// past this the oldest is dropped, and the reaction is still there to be read -
// only its flash is not.
constexpr int kReactionsToFlash = 64;
// What one pending flash is written as: the conversation and the message, which
// together name the reaction wherever the chat is scrolled to.
QString flashKey(const QString& peer, const QString& target)
{
    return peer + "\n" + target;
}

// The reactions offered without being asked for. Anything else a user reaches
// for - typed, or tapped on someone else's chip - is theirs, and is remembered.
const QStringList kStandardReactions = {QStringLiteral("\U0001F44D"),
    QStringLiteral("\u2764\uFE0F"), QStringLiteral("\U0001F602"),
    QStringLiteral("\U0001F389"), QStringLiteral("\U0001F525"), QStringLiteral("\U0001F62E"),
    QStringLiteral("\U0001F622"), QStringLiteral("\U0001F64F"), QStringLiteral("\U0001F440"),
    QStringLiteral("\u2705"), QStringLiteral("\U0001F4AF"), QStringLiteral("\U0001F680"),
    QStringLiteral("\U0001F621"), QStringLiteral("\U0001F44F"), QStringLiteral("\U0001F914"),
    QStringLiteral("\U0001F44E"), QStringLiteral("\U0001F91D"), QStringLiteral("\U0001F529")};

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
    // The long-poll thread pokes this worker when its wait returns, so it is
    // joined here and never left running: a thread that outlives its worker
    // writes into freed memory, which is how a closed account used to end. The
    // errands thread holds the session the same way.
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
                    // The other devices will not see this one. Said rather than
                    // retried: what it carries is a copy, and the next thing this
                    // device sends brings its own.
                    bazarish::log::warn(
                        "could not echo what was sent to our own devices: {}", error.what());
                }
                continue;
            }
            try {
                session_->releasePending(errand.pendingId);
            } catch (const std::exception& error) {
                // The server was momentarily unreachable: the item stays in the
                // mailbox and the next pass offers it again (the interface dedups
                // by protocol id, so nothing is shown twice).
                bazarish::log::warn("pending item not given back: {}", error.what());
            }
            // Either way this device is no longer carrying it - a failed ack left
            // the item where it was, and counting it here forever would park the
            // mail loop for good. The count and the handshake it drives belong to
            // the worker thread, so they are touched there.
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
        // What is left is dropped rather than sent: the account is closing, and
        // an item left in the mailbox is offered again next time.
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
    // The loop owns a copy of everything it touches and a shared flag, so closing
    // the account can leave it to finish on its own.
    eventWaiter_ = std::thread([this, context, running = eventWaiterRunning_]() {
        // One client for the whole loop: each one raises an outbound destination,
        // and building a fresh one per wait meant a new dialer every time. It is
        // kept across a failed wait for the same reason - the failure is the
        // server's or the network's, not this client's.
        std::unique_ptr<bazarish::client::Client> waiter;
        while (running->load()) {
            std::vector<bazarish::client::PendingEntry> waiting;
            try {
                if (!waiter) {
                    waiter = bazarish::client::Session::makeEventClient(context);
                }
                waiting = bazarish::client::Session::waitForMail(*waiter, kEventWaitSeconds);
            } catch (const std::exception& error) {
                // The wait is how this client learns about mail, and there is
                // nothing slower standing behind it to hand the job to: a wait
                // that ended in an error is simply asked again. The same request
                // is also what says whether the server is there at all, so a
                // failure here is what the account's state is drawn from.
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
                // The window closed with nothing in it. There is nothing to
                // read, and asking anyway was a round trip every half minute
                // for an answer this wait had already given.
                continue;
            }
            // The items themselves are fetched here, on this thread, before
            // the pass that applies them is asked for: a round trip each, and
            // the pass would have paid them one after another on the thread
            // every command of the user's queues on. The list is the one the
            // wait came back with, so nothing is asked for twice.
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
            // Something is waiting (or the server's window closed): read the
            // mailbox now, on the worker thread where every other session call
            // runs, and hold the next wait until that pass has finished. A
            // mailbox answers the next wait at once while it still holds what
            // this device is carrying, so without the handshake the loop would
            // spin through its own drain.
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
    // A loop parked on the drain handshake is woken by the same flag, or the
    // join below would wait for a pass this thread is the one meant to run.
    drainDone_.notify_all();
    if (eventWaiter_.joinable()) {
        // Joined, never detached. The loop pokes this worker when its wait
        // returns, so a thread left running past the worker's life writes into
        // freed memory - which is what a closed account used to end in. The wait
        // is bounded (kEventWaitSeconds plus the read slack), and nothing the
        // user is looking at waits for this: the whole teardown happens here, on
        // the worker's own thread.
        eventWaiter_.join();
    }
    // Joined, so the thread object is free to hold the next loop: a loop that
    // ended on its own would otherwise leave this worker unable to start another
    // one, and the account would go on with no way to hear about mail at all.
    eventWaiter_ = std::thread();
    eventWaiterRunning_.reset();
}

void SessionWorker::settleDrain()
{
    // Not settled while the server still lists what this device is carrying: the
    // next wait would be answered by those same items the moment it was asked.
    // A pass that left work behind is a different matter - there the next wait
    // answering at once is exactly what continues the drain.
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
    // What a pass decides to hand back goes out on the errands thread, like the
    // acks the interface asks for: a round trip taken in the middle of a pass is
    // one the next thing the user does waits behind.
    // The server answering is what the connection plate is about, and it
    // answers at the start of a pass rather than at the end of one. A pass that
    // took minutes used to leave the plate saying there was no connection for
    // all of them.
    session_->onServerAnswered([this]() { emit syncReachable(true, {}); });
    session_->setAckSink([this](const std::string& pendingId) { queueAck(pendingId); });
    // The same for the copy of a sent message that this account's other devices
    // are owed: it was the whole of the maintenance pass, and the pass is what
    // the next thing the user does waits behind.
    session_->setSelfSendSink(
        [this](std::string deliveryId, bazarish::Bytes sealed, std::string kind) {
            Errand errand;
            errand.deliveryId = std::move(deliveryId);
            errand.sealed = std::move(sealed);
            errand.kind = std::move(kind);
            queueErrand(std::move(errand));
        });
    // Signing a login needs a key and nothing else, so the front-end gets a
    // signer of its own here: it must not wait behind a sync that may be minutes
    // long to answer a click.
    emit loginSignerReady(session_->loginSigner());
    // The core asks this when the server serves an address no device answered
    // for. Nothing is published until the user says which way.
    session_->onAddressDecision([this](const std::string& served, const std::string& ours) {
        emit addressMismatch(QString::fromStdString(served), QString::fromStdString(ours));
    });
    const bool connected = session_->isConnected();
    emit opened(QString::fromStdString(session_->fingerprint()),
        QString::fromStdString(session_->displayName()), connected);
    // Settings the account carries, so the window shows what is actually in force
    // rather than its own defaults.
    emit accountSettings(session_->acceptCalls(), session_->sendReceipts(),
        session_->sharingAllowed());
    // The names this account already knows it holds, straight from what it
    // stored: the registry is asked nothing here, and the invite has something
    // to show before anybody presses anything.
    {
        const QVariantList rows = aliasHoldingRows(session_->aliasNames());
        if (!rows.isEmpty()) {
            emit aliasHoldings(rows, aliasHoldingsNote(rows, session_->aliasDepositCovers()));
        }
    }
    emitContacts();
    // Whatever the last run left half-done is taken up before anything new is
    // asked of this account - unless nothing at all is to be asked of it: an
    // account opened offline dials nobody, and a contact add is a dial.
    if (startOnline) {
        resumePendingAdds();
    }
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
    // the same channel as a download. Installed once, for as long as the account
    // is open; the download path re-points it at itself while it runs.
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
            break;  // a stage, not bytes: the line above is the whole report
        }
    });
    // Local facts, before anything that touches a server: whether this account
    // holds a destination key and at what address.
    // The invite is data this account already holds; hand it over at open so the
    // sheet has something to show without a request.
    try {
        emit inviteReady(QString::fromStdString(session_->inviteUri()));
    } catch (const std::exception& error) {
        bazarish::log::info("no invite yet: {}", error.what());
    }
    emit i2pKeyState(session_->hasI2pDestination(),
        session_->hasI2pDestination()
            ? QString::fromStdString(session_->i2pAddress())
            : QString());
    // A configured account starts syncing on open; one the user turned off does
    // not, and the switch is the only thing that starts it. Off from the first
    // moment, too: an account opened to be read must not write on its way in.
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
        // Built here because this is where the routing is; it is the contact's
        // own card, so nothing is computed that they did not already hand over.
        contact.sharingRefused = session_->contactSharingRefused(fp);
        try {
            contact.invite = QString::fromStdString(session_->contactInviteUri(fp));
        } catch (const std::exception& error) {
            // A contact we have not been handed everything for yet is the normal
            // early state, and the empty entry is what the card reads to say
            // there is nothing to share - but the reason is said out loud, since
            // "nothing to share" is exactly what a bug here looks like.
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
        // Returning quietly would leave the button reading "Connecting..." for
        // the rest of the session: nothing else ends that state.
        emit actionFailed("no account is open");
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
        // Bootstrapping from public reseed hosts reaches outside the network the
        // user chose, so it is said out loud rather than logged.
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
            // Kept verbatim - this is an address for the I2P engine, not a
            // Bazarish API - and only over TLS: the su3 it fetches is not signed
            // by anybody this client trusts, so what stands between the archive
            // and the network is the transport.
            if (!trimmed.startsWith(QStringLiteral("https://"))) {
                throw std::runtime_error("a reseed address must be an https URL: "
                    + trimmed.toStdString());
            }
            endpoint.reseeds.push_back(trimmed.toStdString());
        }
        if (endpoint.facades.empty()) {
            throw std::runtime_error("enter at least one facade URL");
        }
        // Binding the endpoint stores it, so what the user entered - fingerprint,
        // facades and reseeds together - is kept whatever the round trip below
        // does with it. Saying so here is what stops the editor from offering the
        // previous server back after a connect that failed or never ran.
        session_->connectServer(endpoint);
        emitFacadeInfo();
        // Registering publishes this account's card, which a switched-off account
        // must not do. The settings are saved either way, and that is what the
        // user came to do.
        if (session_->switchedOff()) {
            bazarish::client::setConnectProgressSink({});
            emit actionFailed("this account is switched off: the server connection is saved, "
                              "switch the account on to connect");
            return;
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
        session_->registerAccount();
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
    emit actionOk("Connected");
    emitFacadeInfo();
    startReceiving();
    sync();
}

void SessionWorker::setSyncEnabled(bool on)
{
    // Off is off in both directions. An account that fetches no mail and syncs
    // nothing to its own other devices must not write either: a message sent
    // from it reached the correspondent and left every other device of this
    // account without a trace of it.
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
    // The waiter holds a destination of its own inside its thread; it ends once
    // its outstanding request returns and takes that destination with it. An
    // account that is offline keeps its links released rather than raising new
    // ones nobody asked for.
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
    // On every path out, including the ones that gave up early: the loop that
    // asked for this pass is waiting on it.
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
            // autoAckSurfaced=false: defer acking each surfaced item until the GUI has
            // durably stored it (ackPending via ackAfterReceive), so a crash/restart
            // between fetch and store never loses a message.
            messages = session_->sync(false, Session::kPendingItemsPerPass);
        } catch (const bazarish::client::ApiError& error) {
            // A connect that died halfway - no I2P yet, the server unreachable -
            // leaves the account holding a server this device was never
            // registered with, and nothing else would ever register it: the
            // account is on the server, the device is not. Finish that here
            // rather than answering "not reachable" until the user reconnects
            // by hand.
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
        // Never swallowed: an account that sits at "Connecting" with no reason is
        // undiagnosable, and the reason is often nothing to do with reachability.
        bazarish::log::warn("sync failed: {}", error.what());
        emit syncReachable(false, QString::fromUtf8(error.what()));
        // A send does not go through this server and does not care that a mailbox
        // fetch failed: it is already on its way over I2P and reports for itself.
        return;  // transient: the mail wait asks again, and this pass runs again with it
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
        // The audio came inside the message, so its waveform is drawn from the
        // real thing - computed here, on the worker, and stored with the row.
        if (m.contentType == "voice") {
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
        // The server-side pending id, so the controller can ack this item only after
        // it has durably stored it (deferred ack - see ackAfterReceive).
        map["pendingId"] = QString::fromStdString(m.pendingId);
        emit messageReceived(map);
    }
    // One pass takes a bounded number of items so this thread keeps answering the
    // user. What is left is not scheduled here: the mailbox still holds it, so
    // the wait that woke this pass is answered again the moment it is asked, and
    // the drain continues from there.
    emitContacts();
    emitFacadeInfo();
    refreshCalls();
}

void SessionWorker::refreshCalls()
{
    if (!session_) {
        return;
    }
    // Advance call ring/answer timeouts so a call never rings forever, then flush
    // any finished-call chat-history entries (peer hang-ups are picked up when
    // their message is read, timeouts here), and publish the resulting state.
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
    // Finalize contact-card resolutions that completed off-thread. The thread
    // that runs them holds nothing of this worker, so it cannot say when it is
    // done; this is where that is noticed.
    drainResolvedAdds();
    drainAliasErrands();
    {
        const bazarish::log::Slow timedCalls("keeping the calls current", kSlowStretch);
        refreshCalls();
    }
    // A send the courier finished on its own thread leaves an echo for this
    // account's other devices. Nothing else carries it now that the mailbox is
    // read only when there is mail in it.
    try {
        const bazarish::log::Slow timedEchoes("telling our own devices", kSlowStretch);
        session_->flushPendingEchoes();
    } catch (const std::exception& error) {
        bazarish::log::warn("self-sync of our own sends failed: {}", error.what());
    }
    // A server that has taken the account but not been told to serve it answers
    // everything and delivers nothing. Ask it again while it holds us, and the
    // moment it lets go, publish the routing subscribe could not.
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
    // A destination whose delegation lapses goes dark, and nothing else in the app
    // renews it: the CLI had this loop, the GUI did not.
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
    // The registry has to be told where this account answers, and a destination
    // that has just been re-issued is exactly when it no longer knows. Nothing
    // leaves here for an account that holds no alias, and nothing is asked again
    // until this device's own window has elapsed.
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
        // Returns as soon as the delivery is queued: the watch reports where it
        // gets to and how it ends, from the courier's own thread.
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



void SessionWorker::sendPicture(const QString& peer, const QString& localPath, qint64 localId,
    const QString& e2eId, const QString& replyTo)
{
    try {
        session_->sendPicture(peer.toStdString(), localPath.toStdString(), e2eId.toStdString(),
            watchFor(this, localId), replyTo.toStdString());
    } catch (const std::exception& e) {
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
    // The round trip goes on the errands thread: this runs once per item of a
    // mailbox pass, and what waits behind it is whatever the user asked for
    // next. The item stays counted as carried until the server has actually been
    // told, because that count is what holds the mail loop off - a wait asked
    // while the mailbox still holds these items is answered by them at once.
    queueAck(pendingId.toStdString());
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
        // A user edit replaces text only (the empty keyboard carries nothing, as
        // user messages have none). It is delivery-tracked exactly like a fresh
        // send so the edited bubble's status reflects the edit, not the original.
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
        // Nothing recorded for this id (a plain text message): there was nothing
        // to forget, which is the common case.
        bazarish::log::debug("nothing to forget for a deleted message: {}", error.what());
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

void SessionWorker::addByInvite(const QString& uri, const QString& intro, const QString& opId,
    const QString& requestId)
{
    // Asynchronous: the slow federated card fetch runs off this thread, so sync and
    // the connection are never blocked. The fingerprint (for out-of-band
    // verification) is surfaced when the resolve finalizes (drainResolvedAdds).
    startContactAdd(/*byAlias=*/false, uri, intro, opId, requestId);
}

void SessionWorker::addByAlias(const QString& alias, const QString& intro, const QString& opId)
{
    // Asynchronous, like addByInvite. The alias->fingerprint binding is the one
    // residual trust of the name path; the resolved fingerprint is surfaced for
    // out-of-band verification when the resolve finalizes.
    startContactAdd(/*byAlias=*/true, alias, intro, opId);
}

void SessionWorker::startContactAdd(const bool byAlias, const QString& uriOrAlias,
    const QString& intro, const QString& opId, const QString& requestId)
{
    if (!session_) {
        emit contactAddDone(opId, false, QStringLiteral("no account open"));
        emit actionFailed(QStringLiteral("no account open"));
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
    request.byAlias = byAlias;
    request.uriOrAlias = uriOrAlias.toStdString();
    request.introText = intro.toStdString();
    request.requestId = requestId.isEmpty()
        ? bazarish::toHex(bazarish::randomBytes(kContactRequestIdBytes))
        : requestId.toStdString();
    // Written down before anything is attempted: a client that closes in the
    // middle of an add loses the operation, and this is what lets the next one
    // pick the intent up again. The request keeps its name across that, so what
    // is sent afterwards is the same request and not a second one.
    try {
        session_->notePendingContactAdd({opId.toStdString(), request});
    } catch (const std::exception& e) {
        bazarish::log::warn("contact-add not recorded: {}", e.what());
    }
    // The actual i2p work (build a transient tunnel, dial, fetch the card) happens
    // inside the detached resolve below; surface that we are now in it so the
    // activity panel shows real progress instead of a frozen UI.
    emit contactAddStage(opId, QStringLiteral("Resolving recipient over I2P…"));

    if (!resolvedAdds_) {
        resolvedAdds_ = std::make_shared<ResolvedContactAddQueue>();
    }
    // The detached thread captures only copies and a shared_ptr to the result
    // queue - never session_ or this - so it is safe even if the account is closed
    // while the fetch is in flight. The worker finalizes the result on a later sync.
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

// An add the last run did not finish is taken up again here, with the request it
// already had a name for: the recipient's server recognises the second copy as
// the first and stores one request, not two.
void SessionWorker::resumePendingAdds()
{
    if (!session_) {
        return;
    }
    for (const bazarish::client::Session::PendingContactAdd& pending :
        session_->pendingContactAdds()) {
        const QString opId = QString::fromStdString(pending.opId);
        emit opBegin(opId, QStringLiteral("contact"), QStringLiteral("Adding a contact"),
            QStringLiteral("Resuming after a restart…"));
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
        // Who an alias stands for is only known now. If it is somebody already in
        // the book, the errand ends here: the lookup was worth making, a second
        // contact request is not - it would put a fresh plate in their mailbox
        // for a conversation that is already open on this side.
        if (session_->hasContact(resolved.fingerprint)) {
            emit contactAlreadyKnown(
                entry.opId, QString::fromStdString(resolved.fingerprint));
            continue;
        }
        try {
            emit contactAddStage(entry.opId, QStringLiteral("Sending request…"));
            const std::string fingerprint = session_->commitContactAdd(resolved);
            // Short on purpose: a toast is gone before a 52-character fingerprint
            // can be read, and the fingerprint is in the contact's own card where
            // it can be compared at leisure.
            emit actionOk(QStringLiteral("Contact request sent"));
            emit contactRequestSent(QString::fromStdString(fingerprint),
                QString::fromStdString(resolved.introText),
                QString::fromStdString(resolved.requestId));
            emit contactAddDone(
                entry.opId, true, QStringLiteral("Request sent, awaiting delivery…"));
        } catch (const bazarish::client::ApiError& e) {
            // The recipient's address is taking too many contact requests just
            // now. The request is not lost - it was never stored - so it is
            // worth repeating, and the user is told that is what is happening.
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
        QStringLiteral("Agreeing to a contact request"), QStringLiteral("Telling your server…"));
    try {
        session_->acceptContactRequest(peer.toStdString());
        op.succeed(QStringLiteral("Agreed"));
        emit contactAccepted(peer, true, {});
        emitContacts();  // the acceptance is in the air; the button says so
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
    withSession([&] {
        session_->setDisplayName(name.toStdString());
        emit renamed(QString::fromStdString(session_->displayName()));
        emit actionOk(QStringLiteral("Name updated"));
    });
}

void SessionWorker::renameContact(const QString& peer, const QString& name)
{
    withSession([&] {
        session_->renameContact(peer.toStdString(), name.toStdString());
        emitContacts();  // reflect the new name in the chat list at once
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
        // The other devices keep their copy until they hear it; said out loud
        // rather than left looking like they disagreed.
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
        // Best-effort: this device's own unread state is already correct.
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
        // Best-effort device sync; the local pin already took effect.
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
        // Clear the avatar store entry and re-emit the (now shorter) contact list.
        emit avatarReady(peer, QByteArray());
        emitContacts();
        emit actionOk(QStringLiteral("Contact deleted"));
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
    // Stop what this thread owns, in an order it can answer for: the long poll
    // first (it is the only thing that can outlive the rest), then the timers,
    // then the session itself - which closes the courier, the leases and the I2P
    // links it raised. When this returns nothing of this account is running.
    stopEventWaiter();
    // Before the session goes: the errand in flight is holding it.
    stopErrands();
    if (maintenanceTimer_ != nullptr) {
        maintenanceTimer_->stop();
    }
    if (callTimer_ != nullptr) {
        callTimer_->stop();
    }
    downloadsCancelled_.store(true);
    session_.reset();
    emit stopped();
}

void SessionWorker::askDevicesForContacts()
{
    withSession([&] {
        session_->askDevicesForContacts();
        emit actionOk(QStringLiteral("Asked your other devices for your contacts"));
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
        emit actionFailed(QStringLiteral("no account open"));
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
        QStringLiteral("Publishing your address"), QStringLiteral("Telling your server…"));
    try {
        session_->publishThisDeviceAddress();
        op.succeed(QStringLiteral("Your server serves this address now"));
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
        QStringLiteral("Making a new address"), QStringLiteral("Building it…"));
    try {
        session_->publishFreshAddress();
        op.succeed(QStringLiteral("A new address is published"));
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
            const bazarish::client::DestinationInfo served = session_->serverDestination();
            serverState = QString::fromStdString(served.state);
            // What the server actually serves, not what this device believes it
            // published. The two can differ - another device delegated an address
            // of its own - and the difference is exactly what a user has no other
            // way of seeing.
            emit i2pServedAddress(QString::fromStdString(served.dest));
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

void SessionWorker::refreshDevices()
{
    if (!session_) {
        return;
    }
    WorkerOp op(this, QStringLiteral("devices"), QStringLiteral("status"),
        QStringLiteral("Checking your devices"), QStringLiteral("Asking your server…"));
    try {
        QVariantList devices;
        for (const bazarish::client::Client::DeviceEntry& device : session_->devices()) {
            devices.append(QVariantMap{
                {QStringLiteral("clientId"), QString::fromStdString(device.clientId)},
                {QStringLiteral("current"), device.current},
                {QStringLiteral("queue"), static_cast<qulonglong>(device.queued)},
            });
        }
        op.succeed(QString::number(devices.size()) + QStringLiteral(" device(s)"));
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
            QStringLiteral("Forgetting a device"), QStringLiteral("Telling your server…"));
        try {
            session_->retireDevice(clientId.toStdString());
            op.succeed(QStringLiteral("Forgotten"));
            emit actionOk(QStringLiteral("Device forgotten. Its unread mail is no longer held"));
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
        emit accountClosed(false, QStringLiteral("this account is not open"));
        return;
    }
    WorkerOp op(this, QStringLiteral("account-close"), QStringLiteral("status"),
        QStringLiteral("Deleting the account"), QStringLiteral("Telling your server…"));
    try {
        session_->closeAccountOnServer();
        op.succeed(QStringLiteral("Deleted on the server"));
        emit accountClosed(true, {});
    } catch (const bazarish::client::ApiError& error) {
        // An answer is an answer. A server that refuses to end this account has
        // nothing of it left to end - an operator deleted it first, or the key is
        // not one it knows - and holding on to the profile for that would leave
        // the user unable to finish. A server that did not answer at all, or
        // answered that it is broken, is a different matter: nothing is deleted
        // on a maybe.
        constexpr int kFirstServerErrorStatus = 500;
        if (error.httpStatus > 0 && error.httpStatus < kFirstServerErrorStatus) {
            bazarish::log::info("the server has no account of ours to end ({}); only this "
                                "device's copy goes",
                error.what());
            op.succeed(QStringLiteral("Already gone from the server"));
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
            QStringLiteral("Creating your destination key"), QStringLiteral("Generating…"));
        try {
            const QString address = QString::fromStdString(session_->ensureI2pDestination());
            emit i2pKeyState(true, address);
            op.succeed(address);
            emit actionOk("Personal I2P key created");
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
        emit actionOk("Personal I2P key loaded");
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
        emit actionOk("Personal I2P key deleted");
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
            QStringLiteral("Publishing your destination"),
            QStringLiteral("Delegating it to your server…"));
        try {
            session_->publishRouting();
            op.succeed(QStringLiteral("Published"));
            emit actionOk("Routing published: your card now carries this destination");
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
        emit actionOk("I2P destination revoked");
    } catch (const std::exception& e) {
        emit actionFailed(QString::fromUtf8(e.what()));
    }
    refreshI2pStatus();
}

void SessionWorker::saveAttachment(
    const QString& peer, const QString& e2eId, const QString& destPath, qint64 token)
{
    // Run the request off the worker thread (on the pool) so waiting on the peer
    // never blocks sends or sync; the transfer itself uses its own one-time I2P
    // endpoints. The task captures `this`, session_ and the cancel flag, all kept
    // alive until the pool is drained (see the destructor and openAccount). Emits
    // are skipped once cancelled, so a tearing-down session is never signalled.
    Session* const session = session_.get();
    if (session == nullptr) {
        emit downloadFinished(token, false, QStringLiteral("no open session"));
        return;
    }
    // The download is a request to the peer that announced the file, not a fetch
    // from a store, so it only completes once they answer with an offer.
    const std::string e2eIdStd = e2eId.toStdString();
    const std::string peerStd = peer.toStdString();
    const std::string destStd = destPath.toStdString();
    downloadPool_.start([this, session, e2eIdStd, peerStd, destStd, token]() {
        try {
            // The handler installed when the account opened reports every transfer,
            // in both directions, keyed by peer and file id. Replacing it here left
            // the receiving side with no stages and pointed a concurrent send's
            // events at this one download.
            // Returns at once: the transfer only starts when the sender answers
            // with an offer, so completion is reported by that handler.
            session->requestFile(peerStd, e2eIdStd, destStd);
        } catch (const std::exception& e) {
            if (!downloadsCancelled_.load()) {
                emit downloadFinished(token, false, QString::fromUtf8(e.what()));
            }
        }
    });
}

void SessionWorker::exportAccount(const QString& path, const QString& password)
{
    // Under the progress panel like every other slow thing: the account is
    // read, sealed and written here, and this thread may still be finishing a
    // sync when the click arrives. Silence made it look like nothing happened,
    // which is how one backup became four.
    WorkerOp op(this, QStringLiteral("export"), QStringLiteral("account"),
        QStringLiteral("Exporting your backup"), QStringLiteral("Sealing the account…"));
    try {
        session_->exportAccount(path.toStdString(), password.toStdString());
        op.succeed(QStringLiteral("Backup exported"));
        emit actionOk("Backup exported");
    } catch (const std::exception& e) {
        op.fail(QString::fromUtf8(e.what()));
        emit actionFailed(QString::fromUtf8(e.what()));
    }
}

void SessionController::activateAliasServicing()
{
    if (aliasBusy_) {
        return;
    }
    aliasBusy_ = true;
    emit aliasChanged();
    beginOperation(kAliasOperationId, QStringLiteral("alias"),
        QStringLiteral("Checking your aliases"), QStringLiteral("Asking the registry over I2P…"));
    emit requestActivateAliasServicing();
}

void SessionWorker::rotateServingKey()
{
    try {
        session_->rotateServingKey([this](const std::string& stage) {
            emit servingKeyStage(QString::fromStdString(stage));
        });
        emit servingKeyDone(true, QStringLiteral("The key was changed"));
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
            emit aliasActivationDone(false, QStringLiteral("No account is open."));
        }
        return;
    }
    if (aliasErrandRunning_) {
        // A second run while one is in the air would ask the registry the same
        // question twice over two destinations. The one in the air answers for
        // both - and if this is a press, it now has somebody to report to.
        aliasErrandByHand_ = aliasErrandByHand_ || byHand;
        return;
    }
    // Snapshotted here, on the thread that owns the session; the thread below
    // touches nothing of it.
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
            emit aliasActivationDone(true, QStringLiteral("No alias is registered to this "
                                                          "account"));
        } else if (result.pointed) {
            emit aliasActivationDone(true, QStringLiteral("Your aliases now point here"));
        } else if (session_->aliasUpdatePending()) {
            emit aliasActivationDone(false, QStringLiteral("The registry did not take the "
                                                           "update. It will be tried again."));
        } else {
            emit aliasActivationDone(true, QStringLiteral("Your aliases are up to date"));
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
        emit actionOk(passphrase.isEmpty() ? "This account is no longer password-protected"
                                           : "Password changed");
    } catch (const std::exception& e) {
        emit actionFailed(QString::fromUtf8(e.what()));
    }
}

// ============================ SessionController ============================

// Both travel through a queued signal from the worker, so Qt has to know them
// by name.
namespace {
const int kLoginSignerMetaType
    = qRegisterMetaType<std::shared_ptr<bazarish::client::LoginSigner>>(
        "std::shared_ptr<bazarish::client::LoginSigner>");
const int kContactStateMetaType
    = qRegisterMetaType<QVector<ContactState>>("QVector<ContactState>");
}  // namespace

SessionController::SessionController(QObject* parent)
    : QObject(parent)
{
    // The chat-list search is a name-filtered view over the contacts model; the
    // source keeps its own order (pinned-first, then most-recent), which the proxy
    // preserves. An empty filter shows everything.
    contactsProxy_.setSourceModel(&contacts_);
    contactsProxy_.setFilterRole(ContactListModel::NameRole);
    contactsProxy_.setFilterCaseSensitivity(Qt::CaseInsensitive);

    // Read marks are batched: the timer is the wait, and it runs once per batch.
    readSyncTimer_.setSingleShot(true);
    connect(&readSyncTimer_, &QTimer::timeout, this, &SessionController::flushReadSync);

    worker_ = new SessionWorker();
    worker_->moveToThread(&thread_);
    connect(&thread_, &QThread::finished, worker_, &QObject::deleteLater);


    // Commands -> worker (queued across threads).
    connect(this, &SessionController::requestOpen, worker_, &SessionWorker::openAccount);
    connect(this, &SessionController::requestConnect, worker_, &SessionWorker::connectAndRegister);
    connect(worker_, &SessionWorker::connectProgress, this, &SessionController::onConnectProgress);
    connect(this, &SessionController::requestSendText, worker_, &SessionWorker::sendText);
    connect(this, &SessionController::requestSendFile, worker_, &SessionWorker::sendFile);
    connect(this, &SessionController::requestSendPicture, worker_, &SessionWorker::sendPicture);
    connect(this, &SessionController::requestSendVoice, worker_, &SessionWorker::sendVoice);
    connect(this, &SessionController::requestSendReceipt, worker_, &SessionWorker::sendReceipt);
    connect(this, &SessionController::requestSendReaction, worker_, &SessionWorker::sendReaction);
    connect(this, &SessionController::requestSendCallback, worker_, &SessionWorker::sendCallback);
    connect(this, &SessionController::requestSendCommand, worker_, &SessionWorker::sendCommand);
    connect(this, &SessionController::requestSendEdit, worker_, &SessionWorker::sendEdit);
    connect(this, &SessionController::requestSendDelete, worker_, &SessionWorker::sendDelete);
    connect(this, &SessionController::requestUnsend, worker_, &SessionWorker::dropSentFile);
    connect(this, &SessionController::requestSetAvatar, worker_, &SessionWorker::setAvatar);
    connect(this, &SessionController::requestClearAvatar, worker_, &SessionWorker::clearAvatar);
    connect(this, &SessionController::requestSetDisplayName, worker_,
        &SessionWorker::setDisplayName);
    connect(this, &SessionController::requestRenameContact, worker_, &SessionWorker::renameContact);
    connect(this, &SessionController::requestRemoveContact, worker_, &SessionWorker::removeContact);
    connect(this, &SessionController::requestClearSaved, worker_, &SessionWorker::clearSaved);
    connect(this, &SessionController::requestSetBlocked, worker_, &SessionWorker::setBlocked);
    connect(this, &SessionController::requestSetContactNotifications, worker_,
        &SessionWorker::setContactNotifications);
    connect(this, &SessionController::requestSetContactCalls, worker_,
        &SessionWorker::setContactCalls);
    connect(this, &SessionController::requestSyncChatPin, worker_, &SessionWorker::syncChatPin);
    connect(this, &SessionController::requestSyncRead, worker_, &SessionWorker::syncRead);
    connect(this, &SessionController::requestSyncChatClear, worker_,
        &SessionWorker::syncChatClear);
    connect(this, &SessionController::requestEmitSettings, worker_,
        &SessionWorker::emitSettings);
    connect(this, &SessionController::requestClearChatForEveryone, worker_,
        &SessionWorker::clearChatForEveryone);
    connect(this, &SessionController::requestAddByInvite, worker_, &SessionWorker::addByInvite);
    connect(this, &SessionController::requestAddByAlias, worker_, &SessionWorker::addByAlias);
    connect(this, &SessionController::requestAcceptContact, worker_, &SessionWorker::acceptContact);
    connect(this, &SessionController::requestInviteSig, worker_, &SessionWorker::requestInvite);
    connect(this, &SessionController::requestSignLoginSig, worker_, &SessionWorker::signLogin);
    connect(this, &SessionController::requestPublishThisDeviceAddress, worker_,
        &SessionWorker::publishThisDeviceAddress);
    connect(this, &SessionController::requestPublishFreshAddress, worker_,
        &SessionWorker::publishFreshAddress);
    connect(worker_, &SessionWorker::addressMismatch, this,
        [this](const QString& served, const QString& ours) {
            emit addressNeedsChoice(served, ours);
        });
    connect(worker_, &SessionWorker::loginSignerReady, this,
        [this](std::shared_ptr<bazarish::client::LoginSigner> signer) {
            loginSigner_ = std::move(signer);
        });
    connect(this, &SessionController::requestConnectionLog, worker_,
        &SessionWorker::connectionLog);
    connect(this, &SessionController::requestContactsFromDevices, worker_,
        &SessionWorker::askDevicesForContacts);
    connect(this, &SessionController::requestShutdown, worker_, &SessionWorker::shutdown);
    // The account is closed on its thread; only then does the loop end, and only
    // then is this session finished with.
    connect(worker_, &SessionWorker::stopped, this, [this]() {
        accountDb_.reset();
        store_.close();
        thread_.quit();
    });
    connect(&thread_, &QThread::finished, this, [this]() { emit closed(); });
    connect(this, &SessionController::requestClearConnectionLog, worker_,
        &SessionWorker::clearConnectionLog);
    connect(this, &SessionController::requestSaveAttachment, worker_,
        &SessionWorker::saveAttachment);
    // Download progress / outcome land on the message via the conversation model.
    connect(worker_, &SessionWorker::downloadProgress, this,
        &SessionController::onDownloadProgress);
    connect(worker_, &SessionWorker::servedProgress, this, &SessionController::onServedProgress);
    connect(worker_, &SessionWorker::transferStage, this, &SessionController::onTransferStage);
    connect(worker_, &SessionWorker::servedFinished, this, &SessionController::onServedFinished);
    connect(worker_, &SessionWorker::downloadFinished, this,
        &SessionController::onDownloadFinished);
    connect(this, &SessionController::requestExport, worker_, &SessionWorker::exportAccount);
    connect(this, &SessionController::requestChangePassphrase, worker_,
        &SessionWorker::changePassphrase);
    connect(this, &SessionController::requestRotateServingKey, worker_,
        &SessionWorker::rotateServingKey);
    connect(this, &SessionController::requestActivateAliasServicing, worker_,
        &SessionWorker::activateAliasServicing);
    connect(worker_, &SessionWorker::aliasHoldings, this,
        [this](const QVariantList& rows, const QString& note) {
            aliasHoldings_ = rows;
            aliasNote_ = note;
            emit aliasChanged();
        });
    connect(worker_, &SessionWorker::aliasActivationDone, this, [this](const bool ok,
                                                                   const QString& text) {
        aliasBusy_ = false;
        emit aliasChanged();
        finishOperation(kAliasOperationId, ok, text);
        if (ok) {
            emit actionOk(text);
        } else {
            emit actionFailed(text);
        }
    });
    connect(this, &SessionController::requestSharingAllowed, worker_,
        &SessionWorker::setSharingAllowed);
    connect(worker_, &SessionWorker::servingKeyStage, this, [this](const QString& stage) {
        servingKeyStage_ = stage;
        emit servingKeyChanged();
    });
    connect(worker_, &SessionWorker::servingKeyDone, this, [this](const bool ok,
                                                              const QString& text) {
        servingKeyBusy_ = false;
        servingKeyStage_ = text;
        emit servingKeyChanged();
        if (ok) {
            emit actionOk(text);
        } else {
            emit actionFailed(text);
        }
    });
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
    connect(this, &SessionController::requestSetDelegationDays, worker_,
        &SessionWorker::setDelegationDays);
    connect(this, &SessionController::requestSetAcceptCalls, worker_,
        &SessionWorker::setAcceptCalls);
    connect(this, &SessionController::requestSetSendReceipts, worker_,
        &SessionWorker::setSendReceipts);
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
    connect(worker_, &SessionWorker::accountSettings, this,
        [this](const bool acceptCalls, const bool sendReceipts, const bool sharingAllowed) {
            if (sendReceipts_ != sendReceipts) {
                sendReceipts_ = sendReceipts;
                emit sendReceiptsChanged();
            }
            if (sharingAllowed_ != sharingAllowed) {
                sharingAllowed_ = sharingAllowed;
                emit sharingAllowedChanged();
            }
            if (acceptCalls_ != acceptCalls) {
                acceptCalls_ = acceptCalls;
                emit acceptCallsChanged();
            }
        });
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
        [this](const QVector<ContactState>& contacts, const QStringList& blocked) {
            contactFps_.clear();
            contactState_.clear();
            for (const ContactState& contact : contacts) {
                contactFps_ << contact.fingerprint;
                contactState_.insert(contact.fingerprint, contact);
            }
            blocked_ = blocked;
            syncAgreeingRows();
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
    connect(worker_, &SessionWorker::sendPhase, this, &SessionController::onSendPhase);
    connect(worker_, &SessionWorker::contactRequestSent, this,
        &SessionController::onContactRequestSent);
    connect(worker_, &SessionWorker::contactAddStage, this,
        &SessionController::onContactAddStage);
    connect(this, &SessionController::requestForgetPendingAdd, worker_,
        &SessionWorker::forgetPendingAdd);
    connect(worker_, &SessionWorker::contactAddDone, this, &SessionController::onContactAddDone);
    connect(worker_, &SessionWorker::contactAlreadyKnown, this,
        &SessionController::onContactAlreadyKnown);
    connect(worker_, &SessionWorker::contactAddRateLimited, this,
        &SessionController::onContactAddRateLimited);
    connect(worker_, &SessionWorker::contactAccepted, this,
        &SessionController::onContactAccepted);
    connect(worker_, &SessionWorker::opBegin, this, &SessionController::onOpBegin);
    connect(worker_, &SessionWorker::opDone, this, &SessionController::onOpDone);
    connect(worker_, &SessionWorker::opProgress, this,
        [this](const QString& opId, const QString& status) { updateOperation(opId, status); });
    connect(worker_, &SessionWorker::syncReachable, this, &SessionController::onSyncReachable);
    connect(worker_, &SessionWorker::approvalState, this, &SessionController::onApprovalState);
    connect(worker_, &SessionWorker::devicesReady, this, &SessionController::onDevicesReady);
    connect(this, &SessionController::requestRefreshDevices, worker_,
        &SessionWorker::refreshDevices);
    connect(this, &SessionController::requestForgetDevice, worker_, &SessionWorker::forgetDevice);
    connect(this, &SessionController::requestCloseAccountOnServer, worker_,
        &SessionWorker::closeAccountOnServer);
    connect(worker_, &SessionWorker::accountClosed, this,
        &SessionController::accountClosedOnServer);
    connect(worker_, &SessionWorker::facadeInfo, this, &SessionController::onFacadeInfo);
    connect(worker_, &SessionWorker::actionOk, this, &SessionController::actionOk);
    // A button press closes its own row in the activity panel, and a refusal is
    // said there rather than on the message.
    connect(worker_, &SessionWorker::botActionDone, this,
        [this](const QString& opId, const bool ok, const QString& error) {
            finishOperation(opId, ok, ok ? QStringLiteral("Sent") : error);
        });
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
    connect(worker_, &SessionWorker::connectionLogReady, this,
        &SessionController::connectionLogUpdated);
    connect(worker_, &SessionWorker::serverHello, this, &SessionController::serverHello);
    connect(worker_, &SessionWorker::i2pStatus, this, &SessionController::onI2pStatus);
    connect(worker_, &SessionWorker::i2pKeyState, this, &SessionController::onI2pKeyState);
    connect(worker_, &SessionWorker::i2pServedAddress, this, [this](const QString& address) {
        if (i2pServedAddress_ == address) {
            return;
        }
        i2pServedAddress_ = address;
        emit i2pStatusChanged();
    });
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

    // Every command this controller asks of the worker becomes background work
    // without the command itself having to say so. A command is a signal whose
    // name starts with "request", so they are enumerated here: one connection
    // notes that it was asked for, and one behind the real slot runs when the
    // worker has finished it. Anything added later is covered by being written
    // the same way as the rest.
    const QMetaObject* const meta = metaObject();
    const QMetaMethod noted = meta->method(meta->indexOfSlot("noteCommandQueued()"));
    const QMetaMethod done = SessionWorker::staticMetaObject.method(
        SessionWorker::staticMetaObject.indexOfSlot("noteCommandDone()"));
    // A signal with a default argument is two methods here, and both fire on one
    // emission: bracketing each would count every such command twice.
    QSet<QByteArray> bracketed;
    for (int i = meta->methodOffset(); i < meta->methodCount(); ++i) {
        const QMetaMethod method = meta->method(i);
        if (method.methodType() != QMetaMethod::Signal
            || !method.name().startsWith("request")
            || kSelfDescribingCommands.contains(method.name())
            || bracketed.contains(method.name())) {
            continue;
        }
        bracketed.insert(method.name());
        connect(this, method, this, noted);
        // Behind the command's own slot, which is what makes it the end of it:
        // queued calls reach the worker in the order they were connected.
        connect(this, method, worker_, done);
    }
    connect(worker_, &SessionWorker::commandFinished, this,
        &SessionController::onCommandFinished);
    commandTimer_.setInterval(kCommandTickMs);
    connect(&commandTimer_, &QTimer::timeout, this, &SessionController::showSlowCommands);

    thread_.start();
}

namespace {

// What a command is called in the activity panel. Most read well enough from
// the name of the signal that carries them; these do not.
QString commandTitle(const QByteArray& signalName)
{
    static const QHash<QByteArray, QString> kNamed = {
        {"requestSendReceipt", QStringLiteral("Confirming a message was read")},
        {"requestAckPending", QStringLiteral("Clearing a message from the mailbox")},
        {"requestSendReaction", QStringLiteral("Sending a reaction")},
        {"requestSendEdit", QStringLiteral("Sending an edit")},
        {"requestSendDelete", QStringLiteral("Deleting a message for both sides")},
        {"requestUnsend", QStringLiteral("Withdrawing a file")},
        {"requestAcceptContact", QStringLiteral("Agreeing to a contact request")},
        {"requestEmitSettings", QStringLiteral("Telling your other devices")},
        {"requestSyncRead", QStringLiteral("Marking a chat read")},
        {"requestSyncChatPin", QStringLiteral("Pinning a chat")},
        {"requestSyncChatClear", QStringLiteral("Clearing a chat")},
        {"requestClearChatForEveryone", QStringLiteral("Clearing a chat for both sides")},
        {"requestContactsFromDevices", QStringLiteral("Asking your other devices")},
        {"requestSetSync", QStringLiteral("Going online")},
        {"requestRebuildI2p", QStringLiteral("Rebuilding the I2P destinations")},
        {"requestInviteSig", QStringLiteral("Preparing your invite")},
        {"requestSignLoginSig", QStringLiteral("Signing in")},
    };
    if (const auto found = kNamed.constFind(signalName); found != kNamed.cend()) {
        return found.value();
    }
    // "requestSetDisplayName" -> "Set display name".
    QString words;
    for (int at = static_cast<int>(strlen("request")); at < signalName.size(); ++at) {
        const char letter = signalName.at(at);
        if (letter >= 'A' && letter <= 'Z' && !words.isEmpty()) {
            words += QLatin1Char(' ');
            words += QLatin1Char(letter - 'A' + 'a');
            continue;
        }
        words += QLatin1Char(letter);
    }
    return words;
}

}  // namespace

void SessionController::noteCommandQueued()
{
    const QMetaMethod signal = metaObject()->method(senderSignalIndex());
    QueuedCommand command;
    command.id = QStringLiteral("cmd:") + QString::number(++commandSeq_);
    command.title = commandTitle(signal.name());
    command.queuedAtMs = nowMillis();
    commandQueue_.append(command);
    if (!commandTimer_.isActive()) {
        commandTimer_.start();
    }
}

void SessionController::showSlowCommands()
{
    if (commandQueue_.isEmpty()) {
        commandTimer_.stop();
        return;
    }
    const qint64 now = nowMillis();
    for (int at = 0; at < commandQueue_.size(); ++at) {
        QueuedCommand& command = commandQueue_[at];
        if (now - command.queuedAtMs < kCommandVisibleAfterMs) {
            continue;
        }
        const QString status = at == 0 ? QStringLiteral("Working on it…")
                                       : QStringLiteral("Waiting its turn…");
        if (!command.shown) {
            command.shown = true;
            beginOperation(command.id, QStringLiteral("command"), command.title, status);
            continue;
        }
        updateOperation(command.id, status);
    }
    emit operationsChanged();
}

void SessionController::onCommandFinished()
{
    if (commandQueue_.isEmpty()) {
        return;
    }
    const QueuedCommand command = commandQueue_.takeFirst();
    if (command.shown) {
        finishOperation(command.id, true, QStringLiteral("Done"));
    }
    if (commandQueue_.isEmpty()) {
        commandTimer_.stop();
    }
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
    shutdown();
}

void SessionController::beginShutdown()
{
    if (shuttingDown_) {
        return;
    }
    shuttingDown_ = true;
    if (!thread_.isRunning()) {
        emit closed();
        return;
    }
    // First, from this thread: the worker cannot be asked anything while it is
    // inside a dial, and a dial has a minute of deadline to spend. Taking its
    // facade link out of service is what ends that wait, so the request below is
    // reached in seconds rather than after whatever the account was in the middle
    // of. The account is closing, so the link has no next user.
    client::stopFacadeLinkFor(accountId_.toStdString());
    // Asked, not waited for: the worker stops its own long poll and closes the
    // account on its own thread, and says so.
    emit requestShutdown();
}

void SessionController::shutdown()
{
    shuttingDown_ = true;
    if (thread_.isRunning()) {
        // The worker is deleted as the thread finishes (deleteLater posted on
        // QThread::finished), and with it the session and the account database
        // it holds - so when this returns, nothing here holds the file open.
        thread_.quit();
        thread_.wait();
    }
    accountDb_.reset();
    store_.close();
}

// The account's own store, opened on demand: a second connection to the same
// database the worker's session holds, which is what SQLite is built for.
client::AccountDb& SessionController::accountDb()
{
    if (!accountDb_) {
        accountDb_ = std::make_unique<client::AccountDb>(
            accountPath_.toStdString(), accountPassphrase_.toStdString());
    }
    return *accountDb_;
}

void SessionController::open(const QString& file, const QString& accountId,
    const QString& passphrase, const bool startOnline)
{
    startOnline_ = startOnline;
    accountId_ = accountId;
    accountPath_ = file;
    accountPassphrase_ = passphrase;
    // Everything an account keeps lives in its one encrypted database; the
    // transcript is its largest table, the rest are named rows. A store that
    // will not open answers nothing to every read after it, so the account does
    // not open either.
    if (!store_.open(accountId, file, passphrase)) {
        emit openFailed(QStringLiteral("This profile could not be opened."));
        return;
    }
    const QJsonDocument recents = QJsonDocument::fromJson(
        QByteArray::fromStdString(accountDb().text("recent-reactions")));
    for (const QJsonValue& entry : recents.array()) {
        recentReactions_ << entry.toString();
    }
    if (!recentReactions_.isEmpty()) {
        emit recentReactionsChanged();
    }
    const QJsonDocument flashes = QJsonDocument::fromJson(
        QByteArray::fromStdString(accountDb().text("reactions-to-flash")));
    for (const QJsonValue& entry : flashes.array()) {
        reactionsToFlash_ << entry.toString();
    }
    // There is no outbound queue on disk - by design - so an outgoing message
    // still preparing or still being delivered is one this client was carrying
    // when it closed, not one in flight. They come back explicitly failed, and
    // sending them again is the user's decision, never this client's.
    store_.failUnsentOnLoad(
        DeliveryStatus::Preparing, DeliveryStatus::Delivering, DeliveryStatus::Failed);
    // A contact add that was still running when this client closed has nothing
    // carrying it now. Its line in the conversation said what it was doing, and
    // must stop saying it: it is picked up again below, and either way it is no
    // longer a progress line for an operation that does not exist.
    store_.settleUnfinishedNotes(QStringLiteral("system"), DeliveryStatus::Preparing,
        DeliveryStatus::Received, QStringLiteral("The contact request did not finish."));
    emit requestOpen(file, passphrase, startOnline);
}

void SessionController::connectServer(const QStringList& facadeUrls, const QString& serverFp,
    const QStringList& reseedUrls)
{
    connecting_ = true;
    connectPercent_ = 0;
    connectPhase_ = QStringLiteral("Starting…");
    connectError_.clear();
    beginOperation(kConnectOperationId, QStringLiteral("connect"),
        QStringLiteral("Connecting this account"), connectPhase_);
    emit connectStateChanged();
    emit requestConnect(facadeUrls, serverFp, reseedUrls);
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

QString SessionController::activeFacadeHost() const
{
    return facadeHost(activeFacade_);
}

void SessionController::onFacadeInfo(const QString& activeUrl, const QStringList& configured,
    const QString& serverFp, const QStringList& reseeds)
{
    activeFacade_ = activeUrl;
    configuredFacades_ = configured;
    serverFp_ = serverFp;
    configuredReseeds_ = reseeds;
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
        QStringList reseeds;
        for (const std::string& url : link.reseedUrls) {
            reseeds << QString::fromStdString(url);
        }
        result["reseeds"] = reseeds;
    } catch (const std::exception& error) {
        // Malformed link: return an empty map (the caller checks).
        bazarish::log::debug("server link not parsed: {}", error.what());
    }
    return result;
}

void SessionController::activateConversation(const QString& peer)
{
    // Leaving a conversation ends the wait: what was read in it is owed to the
    // other devices now, not four seconds into the next one.
    flushReadSync();
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
    requestPicturesFor(msgs);
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
    requestPicturesFor(win);
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

void SessionController::openConversationAtMessage(const QString& peer, qint64 localId)
{
    activateConversation(peer);
    // A window ending at the target message (it sits at the window's newest edge),
    // so older context pages in above and newer messages page in below.
    const QVector<StoredMessage> win = store_.olderMessages(peer, localId + 1, kPageSize);
    oldestLoadedId_ = win.isEmpty() ? 0 : win.front().id;
    newestLoadedId_ = win.isEmpty() ? 0 : win.back().id;
    hasMoreOlder_ = !win.isEmpty() && store_.hasMessagesBefore(peer, oldestLoadedId_);
    hasMoreNewer_ = store_.hasMessagesAfter(peer, newestLoadedId_);
    conversation_.setMessages(win);
    requestPicturesFor(win);
    replayTransfersForActivePeer();
    emit pagingChanged();
    emit scrollToMessage(localId);
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
    // A page read from disk carries pictures the same way the first one does.
    // Without this the bubbles scrolled up into are drawn empty: the picture is
    // in the account, and nothing had asked for it.
    requestPicturesFor(older);
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
    requestPicturesFor(newer);
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
    // The saved chat is always in the list, whether or not anything is in it: it
    // is not a contact and cannot be deleted, so nothing else decides it exists.
    // Where it sits is another matter - that is the sort's business, like any
    // other chat's, and it can be pinned the same way.
    if (!savedPeer().isEmpty()) {
        ContactRow saved{savedPeer(), savedChatName(), store_.lastText(savedPeer()),
            store_.lastTime(savedPeer()), 0, store_.isPinned(savedPeer())};
        saved.saved = true;
        rows.push_back(saved);
        known.insert(savedPeer());
    }
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

QString SessionController::savedPeer() const
{
    return fingerprint_;
}

QString SessionController::savedChatName()
{
    return QString::fromLatin1(client::kSavedChatName);
}

QString SessionController::peerName(const QString& id) const
{
    if (!id.isEmpty() && id == fingerprint_) {
        return savedChatName();
    }
    const QString name = contactState_.value(id).name;
    if (!name.isEmpty()) {
        return name;  // the local display name (alias / invite name / rename)
    }
    return shortFingerprint(id);
}

QString SessionController::contactName(const QString& fp) const
{
    return contactState_.value(fp).name;
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

bool SessionController::canWriteTo(const QString& fp) const
{
    return contactState_.value(fp).writable;
}

QString SessionController::contactInvite(const QString& fp) const
{
    return contactState_.value(fp).invite;
}

void SessionController::renameContact(const QString& fp, const QString& name)
{
    if (fp.isEmpty()) {
        return;
    }
    const QString trimmed = name.trimmed();
    // Optimistic local update so the UI reflects the rename at once; the worker
    // persists it and mirrors it to the account's own other devices.
    contactState_[fp].name = trimmed;
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
    // Either way the account's other devices drop their copy: "only for me" means
    // this account, not this device. When it is for everyone the peer is asked as
    // well, and that request is echoed to our own devices by the core.
    if (!forEveryone) {
        emit requestSyncChatClear(peer);
    }
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
    store_.forgetPeer(peer);
    contactFps_.removeAll(peer);
    contactState_.remove(peer);
    openConversation({});  // close the conversation we just deleted
    rebuildChatList();
    refreshUnreadTotal();
    emit requestRemoveContact(peer);
}

void SessionController::clearSavedEverywhere()
{
    store_.clearPeer(savedPeer());
    loadLatestWindow();
    contacts_.touch(savedPeer(), savedChatName(), QString(), nowMillis(), false);
    rebuildChatList();
    emit requestClearSaved();
}

bool SessionController::isBlocked(const QString& peer) const
{
    return blocked_.contains(peer);
}

QVariantList SessionController::blockedList() const
{
    QVariantList rows;
    for (const QString& peer : blocked_) {
        rows.push_back(QVariantMap{{QStringLiteral("fingerprint"), peer},
            {QStringLiteral("name"), peerName(peer)}});
    }
    return rows;
}

void SessionController::setBlocked(const QString& peer, const bool blocked)
{
    if (peer.isEmpty() || isSavedChat(peer)) {
        return;
    }
    if (blocked) {
        if (!blocked_.contains(peer)) {
            blocked_.push_back(peer);
        }
    } else {
        blocked_.removeAll(peer);
    }
    ++contactsRevision_;
    emit contactsRevisionChanged();
    emit requestSetBlocked(peer, blocked);
}

bool SessionController::contactNotifications(const QString& peer) const
{
    return contactState_.value(peer).notifications;
}

bool SessionController::contactCalls(const QString& peer) const
{
    return contactState_.value(peer).calls;
}

void SessionController::setContactNotifications(const QString& peer, const bool on)
{
    if (peer.isEmpty()) {
        return;
    }
    contactState_[peer].notifications = on;
    ++contactsRevision_;
    emit contactsRevisionChanged();
    emit requestSetContactNotifications(peer, on);
}

void SessionController::setContactCalls(const QString& peer, const bool allowed)
{
    if (peer.isEmpty()) {
        return;
    }
    contactState_[peer].calls = allowed;
    ++contactsRevision_;
    emit contactsRevisionChanged();
    emit requestSetContactCalls(peer, allowed);
}

QString SessionController_genE2eId()
{
    return QString::number(QRandomGenerator::global()->generate64(), 16);
}

void SessionController::unblockBeforeWriting(const QString& peer)
{
    if (peer.isEmpty() || !isBlocked(peer)) {
        return;
    }
    // Writing to somebody you blocked is the plainest way of saying you no longer
    // mean to keep them blocked. The block lifts here exactly as the button lifts
    // it - the same one action, told to the account's other devices the same way -
    // rather than the message failing and the user hunting for the switch.
    // Only what a person composes does this; nothing automatic writes into a
    // blocked conversation, and the core still refuses it.
    setBlocked(peer, false);
}

void SessionController::sendText(const QString& text)
{
    if (activePeer_.isEmpty() || text.isEmpty()) {
        return;
    }
    // Consume any reply-in-progress: the reference rides with this one message.
    const QString replyTo = replying_ ? replyingE2eId_ : QString();
    if (replying_) {
        cancelReply();
    }
    deliverText(text, replyTo);
}

void SessionController::sendOffered(const QString& text)
{
    if (activePeer_.isEmpty() || text.isEmpty()) {
        return;
    }
    // A tap on what a message offers is not the composer: whatever is being
    // written there, and whatever a reply is aimed at, is left alone.
    deliverText(text, QString());
}

void SessionController::deliverText(const QString& text, const QString& replyTo)
{
    unblockBeforeWriting(activePeer_);
    StoredMessage m;
    m.peer = activePeer_;
    m.outgoing = true;
    m.type = "text";
    m.e2eId = SessionController_genE2eId();
    m.text = text;
    m.replyTo = replyTo;
    m.ts = nowMillis();
    m.orderKey = m.ts;
    // A note to the saved chat is not delivered to anybody, but it is still
    // written to this account's own server for its other devices - so it is
    // watched like any other send rather than painted green before it has
    // happened. What it skips is the activity row: there is no dialling to show.
    const bool saved = isSavedChat(activePeer_);
    m.status = DeliveryStatus::Preparing;
    m.id = store_.append(m);
    statusById_[m.id] = m.status;
    if (saved) {
        savedSends_.insert(m.id);
    }
    showInActiveView(m, true);
    contacts_.touch(activePeer_, saved ? savedChatName() : QString(), text, m.ts, false);
    if (!saved) {
        beginOperation(QStringLiteral("send:") + QString::number(m.id), QStringLiteral("send"),
            QStringLiteral("To ") + peerName(activePeer_), QStringLiteral("Sending…"),
            activePeer_);
    }
    emit requestSendText(activePeer_, text, m.id, m.e2eId, replyTo);
}

// Records an outgoing attachment in the transcript and the open view and opens
// its activity row. The returned message is empty (id 0) when there is nobody to
// send to or nothing to send.
StoredMessage SessionController::beginAttachmentSend(const QString& fileUrl, const QString& type)
{
    if (activePeer_.isEmpty()) {
        return {};
    }
    unblockBeforeWriting(activePeer_);
    const QString localPath = QUrl(fileUrl).toLocalFile();
    if (localPath.isEmpty()) {
        return {};
    }
    const QString replyTo = replying_ ? replyingE2eId_ : QString();
    if (replying_) {
        cancelReply();
    }
    StoredMessage m;
    m.peer = activePeer_;
    m.outgoing = true;
    m.type = type;
    m.e2eId = SessionController_genE2eId();
    m.replyTo = replyTo;
    m.attName = QUrl(fileUrl).fileName();
    // The local size and mime, so the sender's own bubble draws a real attachment
    // card at once rather than waiting for the upload.
    const QFileInfo info(localPath);
    m.attSize = info.size();
    m.attMime = QMimeDatabase().mimeTypeForFile(info).name();
    // The source path, so a failed send can be tried again without re-picking the
    // file. The bytes are not kept, only the path.
    m.attSrcPath = localPath;
    m.ts = nowMillis();
    m.orderKey = m.ts;
    m.status = DeliveryStatus::Preparing;
    m.id = store_.append(m);
    statusById_[m.id] = DeliveryStatus::Preparing;
    showInActiveView(m, true);
    contacts_.touch(activePeer_, {}, "[" + m.type + "] " + m.attName, m.ts, false);
    beginOperation(QStringLiteral("send:") + QString::number(m.id), QStringLiteral("file-up"),
        m.attName, QStringLiteral("Sending…"), activePeer_);
    return m;
}

void SessionController::sendFile(const QString& fileUrl)
{
    const StoredMessage m = beginAttachmentSend(fileUrl, QStringLiteral("file"));
    if (m.id == 0) {
        return;
    }
    emit requestSendFile(activePeer_, m.attSrcPath, m.id, m.e2eId, m.replyTo);
}

void SessionController::sendPicture(const QString& fileUrl)
{
    const StoredMessage m = beginAttachmentSend(fileUrl, QStringLiteral("image"));
    if (m.id == 0) {
        return;
    }
    // The prepared file is right here, so the sender's bubble draws it without
    // asking anyone: the core stores the same bytes in the account.
    pictureOwners_.insert(m.e2eId, m.id);
    QFile prepared(m.attSrcPath);
    const bool drawable = prepared.open(QIODevice::ReadOnly)
        && PictureStore::instance().put(m.e2eId, prepared.readAll());
    store_.setHasPicture(m.id, drawable);
    conversation_.setPictureReadyForId(m.id, drawable);
    emit requestSendPicture(activePeer_, m.attSrcPath, m.id, m.e2eId, m.replyTo);
}

void SessionController::sendCallback(
    const QString& data, const QString& refMsgId, const QString& label)
{
    if (activePeer_.isEmpty()) {
        return;
    }
    // A button press is silent in the transcript (inline-keyboard semantics): the
    // bot's reply is what appears. What is in flight belongs in the activity
    // panel like every other request, and nothing is written on the message.
    const QString opId = QStringLiteral("bot:") + refMsgId + QStringLiteral(":") + data;
    beginOperation(opId, QStringLiteral("bot"),
        (label.isEmpty() ? data : label) + QStringLiteral(" → ") + peerName(activePeer_),
        QStringLiteral("Sending…"), activePeer_);
    emit requestSendCallback(opId, activePeer_, data, refMsgId);
}

void SessionController::sendCommand(
    const QString& command, const QString& args, const QString& label)
{
    if (activePeer_.isEmpty() || command.isEmpty()) {
        return;
    }
    const QString opId = QStringLiteral("bot-command:") + command;
    beginOperation(opId, QStringLiteral("bot"),
        (label.isEmpty() ? QStringLiteral("/") + command : label) + QStringLiteral(" → ")
            + peerName(activePeer_),
        QStringLiteral("Sending…"), activePeer_);
    emit requestSendCommand(opId, activePeer_, command, args);
}

void SessionController::beginEdit(qint64 localId, const QString& e2eId, const QString& text)
{
    if (replying_) {
        cancelReply();  // editing and replying are mutually exclusive composer modes
    }
    editing_ = true;
    editingLocalId_ = localId;
    editingE2eId_ = e2eId;
    editingText_ = text;
    emit editingChanged();
}

void SessionController::beginReply(
    const QString& e2eId, const QString& previewText, const QString& sender)
{
    if (e2eId.isEmpty()) {
        return;
    }
    if (editing_) {
        cancelEdit();  // mutually exclusive composer modes
    }
    replying_ = true;
    replyingE2eId_ = e2eId;
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
    replyingE2eId_.clear();
    replyingText_.clear();
    replyingSender_.clear();
    emit replyingChanged();
}

QVariantMap SessionController::replyPreview(const QString& e2eId) const
{
    QVariantMap info;
    info[QStringLiteral("found")] = false;
    info[QStringLiteral("localId")] = 0;
    info[QStringLiteral("text")] = QString();
    info[QStringLiteral("sender")] = QString();
    if (e2eId.isEmpty() || activePeer_.isEmpty()) {
        return info;
    }
    const StoredMessage m = store_.messageByE2e(e2eId, activePeer_);
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
        // and clear any prior error, so it then advances on the edit's own
        // delivery instead of showing the original message's state.
        restartDelivery(editingLocalId_);
        emit requestSendEdit(activePeer_, editingE2eId_, editingLocalId_, trimmed);
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
    editingE2eId_.clear();
    editingText_.clear();
    emit editingChanged();
}

void SessionController::deleteMessage(qint64 localId, const QString& e2eId, bool outgoing)
{
    if (activePeer_.isEmpty() || localId == 0) {
        return;
    }
    // Remove our own copy with no trace - which includes the record that would
    // still serve this message's file to the peer if they asked.
    if (!e2eId.isEmpty()) {
        emit requestUnsend(e2eId);
    }
    store_.removeById(localId);
    conversation_.removeById(localId);
    statusById_.remove(localId);
    // Refresh the chat-list preview to whatever the new last message now is.
    contacts_.touch(activePeer_, {}, store_.lastText(activePeer_), store_.lastTime(activePeer_),
        false);
    // Ask the recipient to delete it too, but only for our own message: a peer cannot
    // be told to drop a message we received from them.
    if (outgoing && !e2eId.isEmpty()) {
        emit requestSendDelete(activePeer_, e2eId);
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
        if (descriptor.dest.empty()) {
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
    addByInvite(uri, intro, QString());
}

void SessionController::addByInvite(
    const QString& uri, const QString& intro, const QString& requestId)
{
    const QString problem = inviteProblem(uri);
    if (!problem.isEmpty()) {
        emit actionFailed(problem);
        return;  // no background row for something that cannot be attempted
    }
    // An invite names who it is for, so somebody already in the book is
    // recognised before anything is sent. A second request would put a fresh
    // plate in their mailbox for a conversation that is already open here, and
    // the thing the user wanted is that conversation.
    try {
        const bazarish::Descriptor known
            = bazarish::parseDescriptor(uri.trimmed().toStdString());
        const QString peer = QString::fromStdString(known.fingerprint);
        if (contacts_.has(peer)) {
            openConversation(peer);
            emit actionOk(QStringLiteral("Already in your contacts"));
            return;
        }
    } catch (const std::exception&) {
        // inviteProblem() already vetted the link; the add below reports anything
        // it still cannot read.
    }
    const QString opId = QStringLiteral("contact:") + SessionController_genE2eId();
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
        // Remembered in case the recipient's address is over its cap: the
        // request then has to be sent again, and this is what it takes.
        refusedRequests_.insert(QString::fromStdString(descriptor.fingerprint),
            PendingContactRequest{uri, intro, kContactRetryAttempts, requestId});
    } catch (const std::exception& error) {
        // inviteProblem() already vetted the link, so this cannot normally fire;
        // if it ever does, the add still runs and the panel carries the progress.
        bazarish::log::warn("invite parsed for the chat but not for its peer: {}", error.what());
    }
    emit requestAddByInvite(uri, intro, opId, requestId);
}

void SessionController::onContactAddRateLimited(
    const QString& opId, const QString& fingerprint, const QString& requestId)
{
    finishOperation(opId, false, QStringLiteral("Their address is busy"));
    contactProgressRows_.remove(opId);
    const auto found = refusedRequests_.find(fingerprint);
    if (found != refusedRequests_.end() && found->requestId.isEmpty()) {
        found->requestId = requestId;  // what the first attempt named it
    }
    if (found == refusedRequests_.end() || found->triesLeft <= 0) {
        writeConversationNote(fingerprint,
            QStringLiteral("The request was refused - their server is busy. Try again later."));
        emit contactRetryExhausted(fingerprint);
        return;
    }
    --found->triesLeft;
    writeConversationNote(fingerprint,
        QStringLiteral("Their server is busy. Trying again in %1 seconds (%2 left).")
            .arg(kContactRetrySeconds)
            .arg(found->triesLeft + 1));
    const QString peer = fingerprint;
    QTimer::singleShot(kContactRetrySeconds * kMillisecondsPerSecond, this,
        [this, peer]() { retryContactRequest(peer); });
}

void SessionController::retryContactRequest(const QString& fingerprint)
{
    const auto found = refusedRequests_.find(fingerprint);
    if (found == refusedRequests_.end()) {
        return;
    }
    const PendingContactRequest pending = *found;
    addByInvite(pending.uri, pending.intro, pending.requestId);
    // addByInvite re-registers the entry with a full set of automatic tries; keep
    // the count this attempt is on instead.
    if (const auto again = refusedRequests_.find(fingerprint); again != refusedRequests_.end()) {
        again->triesLeft = pending.triesLeft;
        again->requestId = pending.requestId;
    }
}

QStringList SessionController::agreeingFingerprints() const
{
    QStringList out;
    for (const ContactState& contact : contactState_) {
        if (contact.request == ContactState::eAccepting) {
            out << contact.fingerprint;
        }
    }
    return out;
}

void SessionController::syncAgreeingRows()
{
    for (const QString& fingerprint : agreeingFingerprints()) {
        if (agreeingShown_.contains(fingerprint)
            || operations_.indexOf(QStringLiteral("accept:") + fingerprint) >= 0) {
            continue;  // the command that sends it is saying so already
        }
        agreeingShown_.insert(fingerprint);
        beginOperation(QStringLiteral("agreeing:") + fingerprint, QStringLiteral("contact"),
            QStringLiteral("Agreeing to a contact request"),
            QStringLiteral("Waiting for their server…"), fingerprint);
    }
    for (auto at = agreeingShown_.begin(); at != agreeingShown_.end();) {
        if (contactState_.value(*at).request == ContactState::eAccepting) {
            ++at;
            continue;
        }
        finishOperation(QStringLiteral("agreeing:") + *at, true, QStringLiteral("Agreed"));
        at = agreeingShown_.erase(at);
    }
}

void SessionController::retryContactAdd()
{
    if (activePeer_.isEmpty()) {
        return;
    }
    const auto found = refusedRequests_.constFind(activePeer_);
    if (found == refusedRequests_.cend()) {
        emit actionFailed(QStringLiteral("This add cannot be tried again from here"));
        return;
    }
    // The same request, under the name it already had: the recipient's server
    // recognises the second copy as the first one.
    addByInvite(found->uri, found->intro, found->requestId);
}

void SessionController::addByAlias(const QString& alias, const QString& intro)
{
    const QString opId = QStringLiteral("contact:") + SessionController_genE2eId();
    beginOperation(opId, QStringLiteral("contact"), QStringLiteral("Adding ") + alias,
        QStringLiteral("Preparing…"));
    // Who the alias belongs to is only known once the resolver answers, so the
    // chat opens then (onContactRequestSent); until it does, the activity panel
    // is where the progress shows.
    emit requestAddByAlias(alias, intro, opId);
}

// A system line in a conversation: what is happening with a contact request the
// user is watching, written where they are looking.
void SessionController::writeConversationNote(const QString& peer, const QString& text)
{
    if (peer.isEmpty()) {
        return;
    }
    StoredMessage note;
    note.peer = peer;
    note.type = QStringLiteral("system");
    note.text = text;
    note.ts = nowMillis();
    note.orderKey = note.ts;
    note.status = DeliveryStatus::Received;
    note.id = store_.append(note);
    contacts_.touch(peer, peerName(peer), text, note.ts, false);
    showInActiveView(note, true);
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
    // Preparing while the add runs, so a row left behind by a run that ended
    // early is recognisable as one nothing is working on.
    note.status = DeliveryStatus::Preparing;
    note.id = store_.append(note);
    contactProgressRows_[opId] = note.id;
    contacts_.touch(peer, name, note.text, note.ts, false);
    if (activePeer_ == peer) {
        showInActiveView(note, true);
        return;
    }
    // The chat this belongs to becomes the open one, transcript and all. Making
    // it active without loading its window left the note appended to whichever
    // conversation was on screen, under the new chat's highlight in the list.
    openConversation(peer);
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
    return contactState_.value(fp).request != ContactState::eAnswered;
}

bool SessionController::contactAgreeing(const QString& fp) const
{
    // The optimistic half is this device's own click, so the button answers the
    // press at once; the other half is the account's real state, which outlives
    // the click and comes back if the acceptance never lands.
    return acceptingContact_ == fp
        || contactState_.value(fp).request == ContactState::eAccepting;
}

void SessionController::requestInvite()
{
    emit requestInviteSig();
}

void SessionController::keepThisDeviceAddress()
{
    emit requestPublishThisDeviceAddress();
}

void SessionController::useFreshAddress()
{
    emit requestPublishFreshAddress();
}

void SessionController::signLogin(const QString& challenge)
{
    // Right here on the GUI thread: the signature is a few milliseconds of local
    // work, and the worker may be halfway through a sync.
    //
    // The answer goes back on the next turn of the event loop rather than from
    // inside this call. Emitted straight from here it reached the caller's own
    // handler before that handler had finished - so the button's "a signature is
    // being made" guard was already cleared by the time the click returned, and
    // the copy that follows can spin the event loop, which is an invitation to
    // re-enter a half-finished handler.
    if (loginSigner_) {
        QString blob;
        try {
            blob = QString::fromStdString(loginSigner_->sign(challenge.toStdString()));
        } catch (const std::exception& error) {
            const QString problem = QString::fromUtf8(error.what());
            QMetaObject::invokeMethod(
                this, [this, problem]() { emit actionFailed(problem); }, Qt::QueuedConnection);
            return;
        }
        QMetaObject::invokeMethod(
            this, [this, blob]() { emit loginSigned(blob); }, Qt::QueuedConnection);
        return;
    }
    emit requestSignLoginSig(challenge);
}

void SessionController::askForContacts()
{
    emit requestContactsFromDevices();
}

void SessionController::refreshConnectionLog()
{
    emit requestConnectionLog();
}

void SessionController::clearConnectionLog()
{
    emit requestClearConnectionLog();
}

QVariantMap SessionController::describeLoginChallenge(const QString& challenge) const
{
    QVariantMap described;
    try {
        const bazarish::service::LoginConsumer consumer
            = bazarish::service::readLoginConsumer(challenge.trimmed().toStdString());
        described["ok"] = true;
        described["name"] = QString::fromStdString(consumer.name);
        described["place"] = QString::fromStdString(consumer.place);
        described["role"] = QString::fromStdString(consumer.role);
    } catch (const std::exception& error) {
        // The reason belongs on screen: this is the window where a user decides
        // whether to sign, and "it did not work" decides nothing for them.
        described["ok"] = false;
        described["problem"] = QString::fromUtf8(error.what());
    }
    return described;
}

void SessionController::saveAttachmentToFile(const QString& peer, const QString& e2eId,
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
    // The row carries the transfer's id, which is what stops it: a download that
    // has outlived its point must be endable from the activity panel, the same
    // way a send is.
    beginOperation(QStringLiteral("download:") + QString::number(token), QStringLiteral("file-down"),
        QFileInfo(dest).fileName(), QStringLiteral("Connecting…"), activePeer_, e2eId);
    emit requestSaveAttachment(peer, e2eId, dest, token);
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

void SessionController::rotateServingKey()
{
    if (servingKeyBusy_) {
        return;
    }
    servingKeyBusy_ = true;
    servingKeyStage_ = QStringLiteral("Starting");
    emit servingKeyChanged();
    emit requestRotateServingKey();
}

void SessionController::setSharingAllowed(const bool allowed)
{
    if (sharingAllowed_ == allowed) {
        return;
    }
    sharingAllowed_ = allowed;
    emit sharingAllowedChanged();
    emit requestSharingAllowed(allowed);
}

void SessionController::changePassphrase(const QString& passphrase)
{
    // Kept here too: the transcript store and the account database are opened
    // again on this side, and they open with what this holds.
    accountPassphrase_ = passphrase;
    emit requestChangePassphrase(passphrase);
}

void SessionController::exportAccount(const QString& fileUrl, const QString& password)
{
    const QString localPath = QUrl(fileUrl).toLocalFile();
    if (localPath.isEmpty()) {
        return;
    }
    // The row goes up here, at the click, and not when the account's thread gets
    // round to the work: that thread may be minutes into a sync, and a button
    // that answers nothing until then reads as a button that did nothing. The
    // worker upserts the same row when it starts and closes it when it is done.
    beginOperation(QStringLiteral("export"), QStringLiteral("account"),
        QStringLiteral("Exporting your backup"), QStringLiteral("Waiting for this account…"));
    emit requestExport(localPath, password);
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

void SessionController::setDelegationDays(const int days)
{
    const int bounded = std::clamp(days, minDelegationDays(), maxDelegationDays());
    if (delegationDays_ == bounded) {
        return;
    }
    delegationDays_ = bounded;
    emit requestSetDelegationDays(bounded);
    emit delegationDaysChanged();
}

void SessionController::setAcceptCalls(const bool on)
{
    if (acceptCalls_ == on) {
        return;
    }
    acceptCalls_ = on;
    emit requestSetAcceptCalls(on);
    emit acceptCallsChanged();
}

void SessionController::setSendReceipts(const bool on)
{
    if (sendReceipts_ == on) {
        return;
    }
    sendReceipts_ = on;
    emit requestSetSendReceipts(on);
    emit sendReceiptsChanged();
}

void SessionController::publishPersonalDest()
{
    // The worker answers with a fresh status when it is done, which is what
    // clears this; until then the button says it is working.
    i2pBusy_ = true;
    emit i2pStatusChanged();
    emit requestPublishPersonalDest();
}

void SessionController::disablePersonalDest()
{
    i2pBusy_ = true;
    emit i2pStatusChanged();
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
    i2pBusy_ = false;
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

void SessionController::refreshDevices()
{
    emit requestRefreshDevices();
}

void SessionController::forgetDevice(const QString& clientId)
{
    emit requestForgetDevice(clientId);
}

void SessionController::closeAccountOnServer()
{
    emit requestCloseAccountOnServer();
}

// The one VoiceNote this controller records and plays through, built on first
// use. Both playback kinds end on the same signal, so both are cleared there.
VoiceNote* SessionController::voiceNote()
{
    if (!voice_) {
        voice_ = std::make_unique<VoiceNote>();
        connect(voice_.get(), &VoiceNote::playbackFinished, this, [this]() {
            const QString finished = voicePlaying_;
            voicePlaying_.clear();
            voiceTakePlaying_ = false;
            playbackTimer_.stop();
            voicePositionMs_ = 0;
            emit voiceChanged();
            // Run on to the next voice message in this chat, from either side:
            // a run of them is one thing to listen to, not a row of buttons.
            if (finished.isEmpty() || activePeer_.isEmpty()) {
                return;
            }
            // By protocol id in this conversation, either direction: the
            // outgoing-only lookup that serves delivery receipts found nothing
            // for a message we had received, and the run stopped at the first one.
            const qint64 playedId = store_.messageByE2e(finished, activePeer_).id;
            if (playedId == 0) {
                return;
            }
            const StoredMessage next = store_.nextVoiceAfter(activePeer_, playedId);
            if (!next.e2eId.isEmpty()) {
                playVoice(next.e2eId, 0);
            }
        });
        playbackTimer_.setInterval(kVoiceTickMs);
        connect(&playbackTimer_, &QTimer::timeout, this, [this]() {
            voicePositionMs_ = voice_->playbackPositionMs();
            emit voiceChanged();
        });
        voiceTimer_.setInterval(kVoiceTickMs);
        connect(&voiceTimer_, &QTimer::timeout, this, [this]() {
            voiceLevel_ = voice_->inputLevel();
            if (!voiceRecording_) {
                emit voiceChanged();  // watching the microphone, not filling a take
                return;
            }
            voiceElapsedMs_ = voice_->elapsedMs();
            emit voiceChanged();
            // Full is full, by weight or by the clock. Recording stops on its
            // own - the take is kept, and the user still decides whether it goes.
            if (voiceElapsedMs_ >= kMaxVoiceMs
                || voice_->encodedBytes() >= kMaxVoiceBytes) {
                stopVoiceRecording();
            }
        });
    }
    return voice_.get();
}

void SessionController::startVoiceMonitor()
{
    if (voiceRecording_ || voiceMonitoring_) {
        return;
    }
    voiceError_.clear();
    try {
        voiceNote()->startMonitoring();
    } catch (const std::exception& error) {
        // A microphone that cannot be opened is the very thing this is for.
        voiceError_ = QString::fromUtf8(error.what());
        emit voiceChanged();
        return;
    }
    voiceMonitoring_ = true;
    voiceLevel_ = 0.0;
    voiceTimer_.start();
    emit voiceChanged();
}

void SessionController::stopVoiceMonitor()
{
    if (!voiceMonitoring_) {
        return;
    }
    voiceMonitoring_ = false;
    if (voice_) {
        voice_->stopMonitoring();
    }
    if (!voiceRecording_) {
        voiceTimer_.stop();
    }
    voiceLevel_ = 0.0;
    emit voiceChanged();
}

void SessionController::startVoiceRecording()
{
    if (activePeer_.isEmpty() || voiceRecording_) {
        return;
    }
    // The same microphone cannot be watched and recorded at once.
    stopVoiceMonitor();
    discardVoiceTake();
    voiceError_.clear();
    try {
        voiceNote()->startRecording();
    } catch (const std::exception& error) {
        // Shown in the recorder itself, where the button that failed is.
        voiceError_ = QString::fromUtf8(error.what());
        emit voiceChanged();
        return;
    }
    voiceRecording_ = true;
    voiceElapsedMs_ = 0;
    voiceLevel_ = 0.0;
    voiceTimer_.start();
    emit voiceChanged();
}

void SessionController::stopVoiceRecording()
{
    if (!voiceRecording_ || !voice_) {
        return;
    }
    voiceTimer_.stop();
    voiceRecording_ = false;
    voiceLevel_ = 0.0;
    const qint64 durationMs = voice_->elapsedMs();
    Bytes audio;
    try {
        audio = voice_->stopRecording();
    } catch (const std::exception& error) {
        voiceError_ = QString::fromUtf8(error.what());
        emit voiceChanged();
        return;
    }
    if (audio.empty() || durationMs < kMinVoiceMs) {
        voiceError_ = QStringLiteral("Too short to send.");
        emit voiceChanged();
        return;
    }
    voiceTake_ = QByteArray(
        reinterpret_cast<const char*>(audio.data()), static_cast<qsizetype>(audio.size()));
    voiceTakeMs_ = durationMs;
    voiceTakeWave_ = waveformHex(audio);
    emit voiceChanged();
}

void SessionController::playVoiceTake()
{
    if (voiceTake_.isEmpty()) {
        return;
    }
    if (voiceTakePlaying_) {
        stopVoiceTake();
        return;
    }
    stopVoice();
    voiceTakePlaying_ = true;
    emit voiceChanged();
    try {
        voiceNote()->play(Bytes(voiceTake_.begin(), voiceTake_.end()));
        playbackTimer_.start();
    } catch (const std::exception& error) {
        voiceError_ = QString::fromUtf8(error.what());
        voiceTakePlaying_ = false;
        emit voiceChanged();
    }
}

void SessionController::stopVoiceTake()
{
    if (voice_) {
        voice_->stop();
    }
    playbackTimer_.stop();
    voiceTakePlaying_ = false;
    emit voiceChanged();
}

void SessionController::discardVoiceTake()
{
    if (voiceTakePlaying_) {
        stopVoiceTake();
    }
    voiceTake_.clear();
    voiceTakeMs_ = 0;
    voiceTakeWave_.clear();
    voiceError_.clear();
    emit voiceChanged();
}

void SessionController::sendVoiceTake()
{
    if (voiceTake_.isEmpty() || activePeer_.isEmpty()) {
        return;
    }
    unblockBeforeWriting(activePeer_);
    stopVoiceTake();
    const QByteArray audio = voiceTake_;
    const qint64 durationMs = voiceTakeMs_;
    const QString wave = voiceTakeWave_;
    discardVoiceTake();

    const QString replyTo = replying_ ? replyingE2eId_ : QString();
    if (replying_) {
        cancelReply();
    }
    StoredMessage m;
    m.peer = activePeer_;
    m.outgoing = true;
    m.type = "voice";
    m.e2eId = SessionController_genE2eId();
    m.replyTo = replyTo;
    m.attMime = QStringLiteral("audio/opus");
    m.attSize = audio.size();
    m.attDurationMs = durationMs;
    m.attWave = wave;
    m.ts = nowMillis();
    m.orderKey = m.ts;
    m.status = DeliveryStatus::Preparing;
    m.id = store_.append(m);
    statusById_[m.id] = DeliveryStatus::Preparing;
    showInActiveView(m, true);
    contacts_.touch(activePeer_, peerName(activePeer_), QStringLiteral("[voice]"), m.ts, true);
    beginOperation(QStringLiteral("send:") + QString::number(m.id), QStringLiteral("send"),
        QStringLiteral("To ") + peerName(activePeer_), QStringLiteral("Sending…"), activePeer_);

    emit requestSendVoice(activePeer_, audio, durationMs, m.id, m.e2eId, replyTo);
}

void SessionController::forwardMessage(const QString& e2eId, const QString& toPeer)
{
    if (e2eId.isEmpty() || toPeer.isEmpty()) {
        return;
    }
    const StoredMessage source = store_.messageByE2e(e2eId, activePeer_);
    if (source.id == 0) {
        return;  // not a message this chat holds
    }
    // Passing something on to somebody is writing to them.
    unblockBeforeWriting(toPeer);
    // A forward is a message of this account's own: new id, new row, the content
    // carried over and marked. Nothing of the original travels - not its sender,
    // not its id, not its history of being passed on before.
    StoredMessage m;
    m.peer = toPeer;
    m.outgoing = true;
    m.type = source.type;
    m.e2eId = SessionController_genE2eId();
    m.text = source.text;
    m.forwarded = true;
    m.ts = nowMillis();
    m.orderKey = m.ts;
    m.status = DeliveryStatus::Preparing;

    if (source.type == QStringLiteral("voice")) {
        const QByteArray audio = store_.media(QStringLiteral("voice:") + source.e2eId);
        if (audio.isEmpty()) {
            emit actionFailed(tr("this voice message is no longer on this device"));
            return;
        }
        m.attMime = source.attMime;
        m.attSize = audio.size();
        m.attDurationMs = source.attDurationMs;
        m.attWave = source.attWave;
        m.id = store_.append(m);
        // The core keeps its own copy under the new id when it sends it.
        forwardShown(m, toPeer, QStringLiteral("[voice]"));
        emit requestSendVoice(toPeer, audio, m.attDurationMs, m.id, m.e2eId, QString(), true);
        return;
    }
    if (source.type == QStringLiteral("text")) {
        m.id = store_.append(m);
        forwardShown(m, toPeer, m.text);
        emit requestSendText(toPeer, m.text, m.id, m.e2eId, QString(), true);
        return;
    }
    // A picture or a file is announced from a path, so it can only be passed on
    // while this device still holds the bytes. A picture it does hold - they ride
    // inside the message - is written out for the send to read.
    QString path = source.savedPath;
    if (source.type == QStringLiteral("image")) {
        const QByteArray picture = store_.media(QStringLiteral("picture:") + source.e2eId);
        if (!picture.isEmpty()) {
            const QString scratch = QStandardPaths::writableLocation(QStandardPaths::TempLocation)
                + QStringLiteral("/bazarish-forward-") + m.e2eId + QStringLiteral(".bin");
            QFile out(scratch);
            if (out.open(QIODevice::WriteOnly | QIODevice::Truncate)
                && out.write(picture) == picture.size()) {
                out.close();
                path = scratch;
            }
        }
    }
    if (path.isEmpty() || !QFileInfo::exists(path)) {
        emit actionFailed(tr("save this to your device first, then it can be forwarded"));
        return;
    }
    m.attName = source.attName;
    m.attMime = source.attMime;
    m.attSize = source.attSize;
    m.attSrcPath = path;
    m.id = store_.append(m);
    forwardShown(m, toPeer, source.attName.isEmpty() ? QStringLiteral("[file]") : source.attName);
    if (source.type == QStringLiteral("image")) {
        emit requestSendPicture(toPeer, path, m.id, m.e2eId, QString());
    } else {
        emit requestSendFile(toPeer, path, m.id, m.e2eId, QString());
    }
}

void SessionController::forwardShown(
    const StoredMessage& m, const QString& toPeer, const QString& preview)
{
    statusById_[m.id] = DeliveryStatus::Preparing;
    if (toPeer == activePeer_) {
        showInActiveView(m, true);
    }
    // Passed on to the saved chat, this is a note kept here: the account's own
    // server holding it is the end of the road, and there is no dialling to show
    // in the activity panel either.
    const bool saved = isSavedChat(toPeer);
    if (saved) {
        savedSends_.insert(m.id);
    }
    contacts_.touch(toPeer, saved ? savedChatName() : peerName(toPeer), preview, m.ts, false);
    if (!saved) {
        beginOperation(QStringLiteral("send:") + QString::number(m.id), QStringLiteral("send"),
            QStringLiteral("To ") + peerName(toPeer), QStringLiteral("Forwarding…"), toPeer);
    }
}

void SessionController::cancelVoiceRecording()
{
    if (voiceRecording_ && voice_) {
        voiceTimer_.stop();
        voiceRecording_ = false;
        voiceLevel_ = 0.0;
        voice_->cancelRecording();
    }
    discardVoiceTake();
}

qreal SessionController::voiceSpeed() const
{
    return kVoiceSpeeds.at(static_cast<std::size_t>(voiceSpeedStep_));
}

void SessionController::cycleVoiceSpeed()
{
    voiceSpeedStep_ = (voiceSpeedStep_ + 1) % static_cast<int>(kVoiceSpeeds.size());
    emit voiceChanged();
    // A speed chosen mid-playback applies to what is playing, from where it is.
    if (!voicePlaying_.isEmpty()) {
        const QString playing = voicePlaying_;
        stopVoice();
        playVoice(playing);
    }
}

void SessionController::playVoice(const QString& e2eId, const qint64 fromMs)
{
    // The play button on the message that is playing stops it; a tap on its
    // waveform moves playback instead, which is why the position decides.
    if (voicePlaying_ == e2eId && fromMs < 0) {
        stopVoice();
        return;
    }
    stopVoiceTake();
    voiceNote();
    voicePlaying_ = e2eId;
    voiceSeekMs_ = std::max<qint64>(0, fromMs);
    voicePositionMs_ = voiceSeekMs_;
    emit voiceChanged();
    // Read here, like a picture: a press on play must not wait for the worker.
    onVoiceLoaded(e2eId, store_.media(QStringLiteral("voice:") + e2eId));
}

void SessionController::stopVoice()
{
    if (voice_) {
        voice_->stop();
    }
    playbackTimer_.stop();
    voicePositionMs_ = 0;
    voicePlaying_.clear();
    emit voiceChanged();
}

void SessionController::onVoiceLoaded(const QString& e2eId, const QByteArray& bytes)
{
    if (voicePlaying_ != e2eId || !voice_) {
        return;
    }
    try {
        voice_->play(Bytes(bytes.begin(), bytes.end()), voiceSpeed(), voiceSeekMs_);
        playbackTimer_.start();
    } catch (const std::exception& error) {
        // Audio that will not unpack is a broken message, and saying so beats
        // silence from a button that was just pressed.
        emit actionFailed(QStringLiteral("This voice message is broken."));
        bazarish::log::warn("voice audio did not unpack: {}", error.what());
        voicePlaying_.clear();
        emit voiceChanged();
    }
}


// Brings the pictures of the messages now on screen into the cache. The bytes
// come out of the account through this side's own connection: routing the read
// through the session worker put it behind whatever that thread was doing - a
// connect, a sync - which is why a chat opened on grey squares and filled in
// minutes later.
void SessionController::requestPicturesFor(const QList<StoredMessage>& messages)
{
    for (const StoredMessage& message : messages) {
        if (!message.hasPicture || message.e2eId.isEmpty()) {
            continue;
        }
        pictureOwners_.insert(message.e2eId, message.id);
        if (PictureStore::instance().has(message.e2eId)) {
            continue;
        }
        const QByteArray bytes = store_.media(QStringLiteral("picture:") + message.e2eId);
        if (bytes.isEmpty()) {
            // The bytes travel with the message, so nothing is on its way: this
            // one is broken and has to say so. Left alone it sat as a dark
            // placeholder for a picture that was never going to arrive.
            store_.setHasPicture(message.id, false);
            conversation_.setPictureReadyForId(message.id, false);
            continue;
        }
        if (!PictureStore::instance().put(message.e2eId, bytes)) {
            // What was stored is not a picture: the message is broken and stays
            // marked so.
            store_.setHasPicture(message.id, false);
            conversation_.setPictureReadyForId(message.id, false);
        }
    }
}

void SessionController::savePictureAs(const QString& e2eId, const QString& fileUrl)
{
    const QString path = QUrl(fileUrl).toLocalFile();
    const QByteArray bytes = PictureStore::instance().bytes(e2eId);
    if (path.isEmpty() || bytes.isEmpty()) {
        emit actionFailed(QStringLiteral("This picture is not here to save."));
        return;
    }
    QFile out(path);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate) || out.write(bytes) != bytes.size()) {
        emit actionFailed(QStringLiteral("Could not write ") + path);
        return;
    }
    emit actionOk(QStringLiteral("Picture saved"));
}

void SessionController::copyPicture(const QString& e2eId)
{
    const QImage picture = PictureStore::instance().image(e2eId);
    if (picture.isNull()) {
        emit actionFailed(QStringLiteral("This picture is not here to copy."));
        return;
    }
    QClipboard* const clipboard = QGuiApplication::clipboard();
    if (clipboard == nullptr) {
        emit actionFailed(QStringLiteral("There is no clipboard to copy to."));
        return;
    }
    clipboard->setImage(picture);
    emit actionOk(QStringLiteral("Picture copied"));
}

QUrl SessionController::defaultPictureSaveUrl(const QString& e2eId, const QString& name) const
{
    const QByteArray bytes = PictureStore::instance().bytes(e2eId);
    // The extension follows what the bytes are, not what the message called them.
    const QString suffix = bytes.startsWith(QByteArray::fromHex("89504E47"))
        ? QStringLiteral(".png") : QStringLiteral(".jpg");
    const QString base = name.isEmpty() ? QStringLiteral("picture") : QFileInfo(name).completeBaseName();
    return defaultSaveUrl(base + suffix);
}

void SessionController::onDevicesReady(const QVariantList& devices)
{
    devices_ = devices;
    emit devicesChanged();
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

void SessionController::measureDeviceStorage()
{
    const QVector<ChatWeight> weights = store_.chatWeights();
    QVariantList chats;
    QVector<ChatWeight> ordered = weights;
    std::sort(ordered.begin(), ordered.end(), [](const ChatWeight& a, const ChatWeight& b) {
        return a.rowBytes + a.mediaBytes > b.rowBytes + b.mediaBytes;
    });
    for (const ChatWeight& weight : ordered) {
        QVariantMap chat;
        chat[QStringLiteral("peer")] = weight.peer;
        chat[QStringLiteral("name")] = peerName(weight.peer);
        chat[QStringLiteral("messages")] = weight.messages;
        chat[QStringLiteral("bytes")] = weight.rowBytes + weight.mediaBytes;
        chat[QStringLiteral("mediaCount")] = weight.mediaCount;
        chats << chat;
    }
    const DatabaseFootprint footprint = store_.footprint();
    deviceStorage_.clear();
    deviceStorage_[QStringLiteral("busy")] = deviceStorageBusy_;
    deviceStorage_[QStringLiteral("measuredAt")] = nowMillis();
    deviceStorage_[QStringLiteral("fileBytes")] = footprint.fileBytes;
    deviceStorage_[QStringLiteral("freeBytes")] = footprint.freeBytes;
    deviceStorage_[QStringLiteral("chats")] = chats;
    emit deviceStorageChanged();
}

void SessionController::trimChat(const QString& peer, const int keep)
{
    if (peer.isEmpty()) {
        return;
    }
    runTrim(peer, keep);
}

void SessionController::trimEveryChat(const int keep)
{
    runTrim({}, keep);
}

void SessionController::beginStorageWork(const QString& what, const std::function<void()>& work)
{
    // A trim and a rewrite both take the database exclusively and both run on the
    // thread that draws. Moving them off it would put an arriving message against
    // that lock, and a message that cannot be stored is worse than a window that
    // waits - so the window is deliberately held, and the only thing that must not
    // happen is holding it before it has said why. The delay is what buys the
    // frame that paints the notice; a queued call alone can beat it to the screen.
    deviceStorageBusy_ = true;
    deviceStorage_[QStringLiteral("busy")] = true;
    deviceStorage_[QStringLiteral("busyWhat")] = what;
    emit deviceStorageChanged();
    QTimer::singleShot(kBusyPaintDelayMs, this, work);
}

void SessionController::endStorageWork()
{
    deviceStorageBusy_ = false;
    measureDeviceStorage();
}

void SessionController::compactDatabase()
{
    if (deviceStorageBusy_) {
        return;
    }
    beginStorageWork(QStringLiteral("Compacting the database"), [this]() {
        QString reason;
        const bool rebuilt = store_.rebuild(reason);
        const qint64 before = deviceStorage_.value(QStringLiteral("fileBytes")).toLongLong();
        endStorageWork();
        if (!rebuilt) {
            emit actionFailed(QStringLiteral("The database was not compacted: ") + reason);
            return;
        }
        const qint64 after = deviceStorage_.value(QStringLiteral("fileBytes")).toLongLong();
        // What it actually returned, rather than what it might have: the figure
        // the user is watching is the one on disk.
        emit actionOk(QStringLiteral("The database was compacted; ")
            + humanBytes(std::max<qint64>(0, before - after))
            + QStringLiteral(" came back to the disk"));
    });
}

void SessionController::runTrim(const QString& peer, const int keep)
{
    if (deviceStorageBusy_) {
        return;
    }
    beginStorageWork(QStringLiteral("Trimming and compacting the database"),
        [this, peer, keep]() {
            qint64 removed = 0;
            try {
                removed = peer.isEmpty() ? store_.pruneEveryChatToLatest(keep)
                                         : store_.pruneToLatest(peer, keep);
            } catch (const std::exception& error) {
                // Nothing was removed: the whole trim is one transaction and it
                // rolled back.
                endStorageWork();
                emit actionFailed(QString::fromUtf8(error.what()));
                return;
            }
            QString reason;
            const bool rebuilt = store_.rebuild(reason);
            loadLatestWindow();
            rebuildChatList();
            refreshUnreadTotal();
            endStorageWork();
            if (rebuilt) {
                emit actionOk(QStringLiteral("Removed ") + QString::number(removed)
                    + QStringLiteral(" messages and compacted the database"));
                return;
            }
            // The trim itself committed. Reporting this as a failure would say
            // the messages are still there, and they are not.
            emit actionFailed(QStringLiteral("Removed ") + QString::number(removed)
                + QStringLiteral(" messages, but the space has not been returned to the disk: ")
                + reason + QStringLiteral(". Trimming again returns it."));
        });
}

void SessionController::onOpened(
    const QString& fingerprint, const QString& displayName, bool connected)
{
    fingerprint_ = fingerprint;
    displayName_ = displayName;
    connected_ = connected;
    emit identityChanged();
    emit connectedChanged();
    // A connected account starts syncing on open, so it comes up online - unless
    // it was opened offline, which is an account read without being switched on.
    if (const bool nowOnline = connected && startOnline_; online_ != nowOnline) {
        online_ = nowOnline;
        emit onlineChanged();
    }
}

void SessionController::onConnectionChanged(bool connected, const QString& connectionNote)
{
    // Any outcome ends the connect: success clears the screen's busy state,
    // failure leaves the reason on it instead of a silent button.
    if (connecting_) {
        connecting_ = false;
        connectPhase_.clear();
        connectError_ = connected ? QString() : connectionNote;
        finishOperation(kConnectOperationId, connected,
            connected ? QStringLiteral("Connected") : connectionNote);
        emit connectStateChanged();
    }
    connected_ = connected;
    emit connectedChanged();
    if (online_ != connected) {
        online_ = connected;
        emit onlineChanged();
    }
}

void SessionController::goOnline()
{
    // Whatever this account was opened as, it is switched on now.
    startOnline_ = true;
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

void SessionController::onApprovalState(const bool pending, const QString& note)
{
    if (awaitingApproval_ == pending && approvalNote_ == note) {
        return;
    }
    awaitingApproval_ = pending;
    approvalNote_ = note;
    emit approvalChanged();
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
    const QString incomingId = message.value("e2eId").toString();

    // Idempotent receive, before anything acts on the message. The mailbox is
    // at-least-once: an item whose ack was lost, or that a sender retried, is
    // legitimately re-offered and arrives here again with the same id. Dedup
    // against the transcript - if this conversation already holds an incoming
    // message or note with this id, this is that redelivery. It has to come first:
    // the handlers below return early, and a note stored by one of them (a contact
    // request agreed to, a cleared chat) would otherwise be written once per
    // redelivery. (A read receipt is only sent on a real read, handled by
    // markReadThroughRow.)
    if (!incomingId.isEmpty() && store_.idForIncomingE2e(incomingId, peer) != 0) {
        return;
    }
    // The same, for what another device of ours sent: an echo names the message
    // it echoes, so a second copy of it - a redelivery, or a message this device
    // wrote and then heard about - is that message, not another one. Without
    // this the conversation grew a second bubble for one thing that was sent
    // once.
    if (!incomingId.isEmpty() && message.value("sentByUs").toBool()
        && store_.idForE2e(incomingId) != 0) {
        return;
    }

    // Call signalling drives the call screen via callStateChanged, never the
    // chat list.
    if (type.startsWith(QStringLiteral("call."))) {
        return;
    }

    // One invitation per conversation. A request that is sent again - the same
    // one repeated, or a fresh one after the peer removed us - is the same
    // invitation, and a chat that grows a second plate for it reads as two people
    // asking. What it carries (their routing, the pass they hand over) has already been
    // applied by the core; only the plate is dropped.
    if (type == QStringLiteral("contact.request") && !message.value("sentByUs").toBool()
        && store_.oldestOfType(peer, type, /*outgoing=*/false) != 0) {
        bazarish::log::info("a second contact request from {} keeps the plate it already has",
            peer.toStdString());
        return;
    }

    // Another device of ours emptied its copy of a conversation.
    if (type == "device.chat-clear") {
        const QString cleared = message.value("ref").toString();
        if (!cleared.isEmpty()) {
            store_.clearPeer(cleared);
            if (activePeer_ == cleared) {
                loadLatestWindow();
            }
            contacts_.touch(cleared, peerName(cleared), store_.lastText(cleared),
                store_.lastTime(cleared), false);
            contacts_.setUnread(cleared, store_.unreadCount(cleared));
            refreshUnreadTotal();
        }
        return;
    }

    // The account was renamed, or an account-wide answer changed, on another
    // device of ours. The core has applied it; the window catches up.
    if (type == "device.account-name") {
        const QString name = message.value("text").toString();
        if (!name.isEmpty() && name != displayName_) {
            displayName_ = name;
            emit identityChanged();
        }
        return;
    }
    if (type == "device.account-prefs") {
        emit requestEmitSettings();
        return;
    }

    // Another device of ours emptied the saved chat. Nothing is announced: this is
    // the user's own action arriving late.
    if (type == "device.saved-clear") {
        store_.clearPeer(savedPeer());
        if (activePeer_ == savedPeer()) {
            loadLatestWindow();
        }
        contacts_.touch(savedPeer(), savedChatName(), QString(), nowMillis(), false);
        rebuildChatList();
        return;
    }

    // Another device of ours removed a contact: the core has already dropped it
    // here, so what is left is the conversation and the row it sat in.
    if (type == "device.contact-remove") {
        const QString gone = message.value("ref").toString();
        if (!gone.isEmpty()) {
            store_.forgetPeer(gone);
            contacts_.remove(gone);
            if (activePeer_ == gone) {
                openConversation({});
            }
            rebuildChatList();
            refreshUnreadTotal();
        }
        return;
    }

    // A block, a per-contact switch, or a contact request agreed to on another
    // device. The core applied it; the interface only re-reads what it shows -
    // which is what takes the Agree button off a request already answered.
    if (type == "device.contact-block" || type == "device.contact-prefs"
        || type == "device.contact-accepted") {
        ++contactsRevision_;
        emit contactsRevisionChanged();
        return;
    }

    // A pin/unpin synced from another of our devices (ref = the pinned chat, text =
    // "1"/"0"): apply it to the local pin list and re-sort. Silent - no bubble.
    if (type == "device.read") {
        // Another device of ours has read this conversation. What it read is
        // already here or it is not; either way nothing else changes.
        const QString readPeer = message.value("ref").toString();
        const qint64 through = message.value("text").toString().toLongLong();
        if (!readPeer.isEmpty() && through > 0) {
            store_.applyReadThrough(readPeer, through);
            lastReadAckedId_[readPeer]
                = qMax(lastReadAckedId_.value(readPeer, 0), store_.lastReadId(readPeer));
            contacts_.setUnread(readPeer, store_.unreadCount(readPeer));
            refreshUnreadTotal();
        }
        return;
    }

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
        const qint64 localId = store_.idForE2e(ref);
        if (localId == 0) {
            // The receipt outran the message it is about. On a second device the
            // message arrives as an echo of what the first device sent, and
            // nothing promises the mailbox hands the two over in that order:
            // dropping the receipt here left the bubble amber for good. Kept, and
            // applied when the message lands.
            bazarish::log::info("receipt for {} arrived before the message it is about",
                ref.toStdString());
            receiptsAhead_[peer].insert(ref);
            return;
        }
        // Said out loud on both sides: a receipt reaches an account, and every
        // device of it takes its own copy from the mailbox. When one device shows
        // green while another stays amber, the two lines are what separate a
        // receipt that arrived late from one that arrived and was not applied.
        bazarish::log::info("receipt for {} applied to row {}", ref.toStdString(), localId);
        if (localId != 0) {
            markOutgoingRead(peer, localId);
        }
        return;
    }

    // A reaction: record the reactor's emoji against the target message and
    // re-drive the chips. Never a chat bubble. The reactor is the peer who sent it.
    if (type == "reaction") {
        // Whose reaction it is: the peer's, or ours when this is another device of
        // ours saying what we did there.
        const bool ours = message.value("sentByUs").toBool();
        const QString reactor = ours ? fingerprint_ : peer;
        const QString target = message.value("ref").toString();
        const QString emoji = message.value("text").toString();
        store_.setReaction(peer, target, reactor, emoji);
        // Somebody put this on one of our messages. It gets a flash when the
        // conversation is next looked at, and - unless it was us, on another
        // device of ours - it is announced like an arrival, with its own sound.
        if (!ours && !emoji.isEmpty()) {
            noteReactionToFlash(peer, target);
            if (contactNotifications(peer)) {
                emit reactionNotification(peer, peerName(peer), emoji);
            }
        }
        ++reactionsRevision_;
        emit reactionsRevisionChanged();
        return;
    }

    // An in-place edit of a message this peer previously sent us: update it
    // where it sits instead of adding a new bubble. Scoped to incoming-from-peer
    // in the store, so a peer can only edit its own messages.
    if (type == "edit") {
        // Scoped to incoming-from-peer in the store, so a peer can only edit its
        // own messages. An edit echoed from another device of ours is about a
        // message of ours, so it is looked up unscoped.
        const qint64 localId = message.value("sentByUs").toBool()
            ? store_.idForE2e(message.value("ref").toString())
            : store_.idForIncomingE2e(message.value("ref").toString(), peer);
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
        // As with an edit: ours refers to a message of ours.
        const qint64 localId = message.value("sentByUs").toBool()
            ? store_.idForE2e(message.value("ref").toString())
            : store_.idForIncomingE2e(message.value("ref").toString(), peer);
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
        sys.e2eId = incomingId;  // so a redelivery is recognised as one
        sys.type = QStringLiteral("system");
        sys.text = message.value("sentByUs").toBool()
            ? QStringLiteral("You cleared the chat for everyone.")
            : peerName(peer) + QStringLiteral(" cleared the chat.");
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
        sys.e2eId = incomingId;  // so a redelivery is recognised as one
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

    // Control content with nothing to show: a button press, or a type a newer
    // client sends that this one cannot render, carries no text.
    // Stored, each became an empty bubble that also counted as unread. The item is
    // still acked - ackAfterReceive runs off the same signal - so it does not come
    // back.
    // Audio that rides inside the message has no name and nothing to fetch, so
    // "nothing to show" has to ask about the bytes too - without this a voice
    // message was received, acked and receipted, and then dropped here.
    if (message.value("text").toString().isEmpty()
        && message.value("attName").toString().isEmpty()
        && message.value("attRef").toString().isEmpty()
        && message.value("attSize").toLongLong() <= 0
        && message.value("keyboard").toString().isEmpty()) {
        bazarish::log::info("silent control message ({}) not shown", type.toStdString());
        return;
    }

    StoredMessage m;
    m.peer = peer;
    // Another device of ours sent this; it belongs on our side of the chat.
    m.outgoing = message.value("sentByUs").toBool();
    m.type = type;
    m.e2eId = message.value("e2eId").toString();
    m.text = message.value("text").toString();
    m.replyTo = message.value("replyTo").toString();
    m.forwarded = message.value("forwarded").toBool();
    m.attName = message.value("attName").toString();
    m.attMime = message.value("attMime").toString();
    m.attSize = message.value("attSize").toLongLong();
    m.attDurationMs = message.value("attDurationMs").toLongLong();
    m.attWave = message.value("attWave").toString();
    m.attRef = message.value("attRef").toString();
    m.keyboard = message.value("keyboard").toString();
    // Order by and display the sender's own sentAt (ms): a recent burst that
    // arrived out of order is reordered into place; a long-delayed arrival is
    // appended at the end as new (docs-main Messages.md "Ordering and timestamps").
    const Placement placement = placeReceived(message.value("sentAt").toLongLong(), nowMillis());
    m.ts = placement.displayTs;
    m.orderKey = placement.orderKey;
    // An echo exists because the send it echoes was stored by the recipient's
    // server - the device that sent it writes the echo only then - so it lands
    // here in the same amber state the sender is showing, and the contact's read
    // receipt (which reaches every device of this account) turns it green here
    // too. A note to the saved chat is the exception: there is no correspondent
    // to read it, so it is finished the moment our own server holds it.
    m.status = m.outgoing
        ? (isSavedChat(peer) ? DeliveryStatus::Delivered : DeliveryStatus::AtRecipientServer)
        : DeliveryStatus::Received;
    m.id = store_.append(m);
    // A receipt that arrived before this message was here has been waiting for
    // it. Applied now, so an echo of our own send does not sit amber forever.
    if (m.outgoing && !m.e2eId.isEmpty() && receiptsAhead_.value(peer).contains(m.e2eId)) {
        receiptsAhead_[peer].remove(m.e2eId);
        markOutgoingRead(peer, m.id);
    }

    showInActiveView(m, false);
    // A picture arrives inside the message, so there is nothing to fetch: the
    // core has already put it in the account, and this reads it back to draw.
    if (m.type == QStringLiteral("image") && !m.e2eId.isEmpty()) {
        pictureOwners_.insert(m.e2eId, m.id);
        // The core has just stored it; read it back through this side's own
        // connection so the bubble draws it now, not after the next sync.
        const bool drawable = PictureStore::instance().put(
            m.e2eId, store_.media(QStringLiteral("picture:") + m.e2eId));
        store_.setHasPicture(m.id, drawable);
        conversation_.setPictureReadyForId(m.id, drawable);
    }
    QString preview = m.text;
    if (preview.isEmpty() && !m.attName.isEmpty()) {
        preview = "[" + type + "] " + m.attName;
    }
    contacts_.touch(peer, peerName(peer), preview, m.ts, false);
    // The unread badge is the persistent count of incoming messages past the read
    // high-water (set when messages actually scroll into view), not a running
    // increment - so it stays accurate across restarts and partial reads.
    contacts_.setUnread(peer, store_.unreadCount(peer));
    if (!m.outgoing && contactNotifications(peer)) {
        // An echo of our own message from another device is not news to anybody,
        // and neither is a contact the user has asked to keep quiet - the chat
        // list still counts it, so nothing is hidden, it only stays silent.
        emit messageNotification(peer, peerName(peer));
    }
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

void SessionController::restartDelivery(qint64 localId)
{
    statusById_[localId] = DeliveryStatus::Preparing;
    store_.updateStatus(localId, DeliveryStatus::Preparing);
    conversation_.setStatusForId(localId, DeliveryStatus::Preparing);
    conversation_.setErrorForId(localId, {});
}

void SessionController::bumpStatus(qint64 localId, int status)
{
    // Never downgrade (e.g. "yellow" arriving after "green"); failed is terminal.
    const int current = statusById_.value(localId, DeliveryStatus::Preparing);
    if (status != DeliveryStatus::Failed && status <= current) {
        return;
    }
    statusById_[localId] = status;
    store_.updateStatus(localId, status);
    conversation_.setStatusForId(localId, status);
}

void SessionController::onSendProgress(qint64 localId, int state)
{
    bumpStatus(localId, state);
    if (state == DeliveryStatus::AtRecipientServer) {
        // There is no handover to a server of ours any more, so the row follows
        // the whole journey: it is done when the recipient's server has signed for
        // the envelope. Reading shows up on the message itself.
        finishOperation(QStringLiteral("send:") + QString::number(localId), true,
            QStringLiteral("Handed to the recipient's server"));
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
    const QString& peer, const QString& e2eId, const QString& stage)
{
    TransferProgress& progress = transfers_[e2eId];
    progress.peer = peer;
    progress.stage = stage;
    const StoredMessage m = store_.messageByE2e(e2eId, peer);
    if (m.id == 0) {
        return;
    }
    // A transfer runs long and can be stopped, so it gets a row of its own the
    // moment it starts - a send row from an hour ago is not that row.
    const QString opId = (m.outgoing ? QStringLiteral("send:") : QStringLiteral("download:"))
        + QString::number(m.id);
    if (operations_.indexOf(opId) < 0) {
        beginOperation(opId, m.outgoing ? QStringLiteral("file-up") : QStringLiteral("file-down"),
            m.attName.isEmpty() ? QStringLiteral("file") : m.attName, stage, peer, e2eId);
    } else {
        // The send's own row, opened when the file was announced: now there is a
        // transfer behind it, and it is stoppable.
        operations_.setCancelId(opId, e2eId);
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
    const QString& peer, const QString& e2eId, qint64 sent, qint64 total)
{
    TransferProgress& progress = transfers_[e2eId];
    progress.peer = peer;
    progress.sent = sent;
    progress.total = total;
    const StoredMessage m = store_.messageByE2e(e2eId, peer);
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
    const QString& peer, const QString& e2eId, const bool ok, const QString& error)
{
    const StoredMessage m = store_.messageByE2e(e2eId, peer);
    if (m.id == 0 || peer != activePeer_) {
        // Nobody is looking at this conversation: remember the outcome so opening
        // it shows what happened, instead of a bubble that quietly lost its bar.
        TransferProgress& kept = transfers_[e2eId];
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
    transfers_.remove(e2eId);
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

void SessionController::cancelTransfer(const QString& e2eId)
{
    emit requestCancelTransfer(e2eId);
}

void SessionController::replayTransfersForActivePeer()
{
    QStringList settled;
    for (auto it = transfers_.constBegin(); it != transfers_.constEnd(); ++it) {
        if (it.value().peer != activePeer_) {
            continue;
        }
        const StoredMessage m = store_.messageByE2e(it.key(), activePeer_);
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

void SessionController::onDownloadFinished(qint64 token, bool ok, const QString& error)
{
    const QString path = pendingSavePath_.take(token);
    const QString opId = QStringLiteral("download:") + QString::number(token);
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
        // The amber state was already set through sendProgress, which is also what
        // closes the activity row. Just clear any prior failure note.
        conversation_.setErrorForId(localId, {});
        // A note to the saved chat is finished the moment this account's own
        // server holds it: there is no correspondent to read it and no receipt
        // coming, so amber would be a wait for something that never arrives.
        if (savedSends_.remove(localId)) {
            bumpStatus(localId, DeliveryStatus::Delivered);
        }
        return;
    }
    savedSends_.remove(localId);
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
    const QString human = humanDeliveryPhase(phase);
    // Where the send has got to, on its activity row (a no-op if the row already
    // settled) and, while it is retrying, under the bubble itself: a message that
    // is being tried again should say so where the user is looking.
    updateOperation(QStringLiteral("send:") + QString::number(localId), human);
    if (isRetryPhase(phase)) {
        conversation_.setErrorForId(localId, human);
    }
}

void SessionController::resendText(qint64 localId, const QString& text, const QString& e2eId)
{
    if (activePeer_.isEmpty() || text.isEmpty()) {
        return;
    }
    // Reset to "sending" and clear the prior error, then re-dispatch with the
    // SAME protocol id so the recipient's server still deduplicates it (a retry
    // must never double-deliver).
    restartDelivery(localId);
    // Preserve the original reply reference on a resend.
    const QString replyTo = store_.messageByE2e(e2eId, activePeer_).replyTo;
    // A resend is a send: it travels the same way and takes the same time, so it
    // belongs in the activity panel like the first attempt did.
    beginOperation(QStringLiteral("send:") + QString::number(localId), QStringLiteral("send"),
        QStringLiteral("To ") + peerName(activePeer_), QStringLiteral("Sending again…"),
        activePeer_);
    emit requestSendText(activePeer_, text, localId, e2eId, replyTo);
}

void SessionController::resendFile(qint64 localId, const QString& e2eId)
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
    restartDelivery(localId);
    const QString replyTo = store_.messageByE2e(e2eId, activePeer_).replyTo;
    const StoredMessage stored = store_.messageByE2e(e2eId, activePeer_);
    beginOperation(QStringLiteral("send:") + QString::number(localId), QStringLiteral("file-up"),
        stored.attName.isEmpty() ? QStringLiteral("file") : stored.attName,
        QStringLiteral("Sending again…"), activePeer_);
    emit requestSendFile(activePeer_, srcPath, localId, e2eId, replyTo);
}

void SessionController::resendVoice(qint64 localId, const QString& e2eId)
{
    if (activePeer_.isEmpty()) {
        return;
    }
    // A voice take has no file behind it: the recording lives in the account, so
    // that is where a resend reads it from.
    const QByteArray audio = store_.media(QStringLiteral("voice:") + e2eId);
    if (audio.isEmpty()) {
        emit actionFailed(tr("this voice message is no longer on this device"));
        return;
    }
    const StoredMessage stored = store_.messageByE2e(e2eId, activePeer_);
    restartDelivery(localId);
    beginOperation(QStringLiteral("send:") + QString::number(localId), QStringLiteral("send"),
        QStringLiteral("To ") + peerName(activePeer_), QStringLiteral("Sending again…"),
        activePeer_);
    emit requestSendVoice(
        activePeer_, audio, stored.attDurationMs, localId, e2eId, stored.replyTo, stored.forwarded);
}

void SessionController::markOutgoingRead(const QString& peer, qint64 uptoId)
{
    // Persist the green high-water (covers paged-out rows too). Only messages
    // known to have reached the recipient's server are carried up by a later
    // receipt: reading one message says the earlier ones were seen, but says
    // nothing about one that never got there, and a message still sitting in our
    // own server's queue was turning green on the strength of the next one.
    store_.markOutgoingReadUpTo(peer, uptoId, DeliveryStatus::Delivered,
        DeliveryStatus::AtRecipientServer, DeliveryStatus::AtRecipientServer);
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
    accountDb().putText("recent-reactions", text.toStdString());
    emit recentReactionsChanged();
}

void SessionController::noteReactionToFlash(const QString& peer, const QString& target)
{
    const QString key = flashKey(peer, target);
    reactionsToFlash_.removeAll(key);
    reactionsToFlash_.append(key);
    while (reactionsToFlash_.size() > kReactionsToFlash) {
        reactionsToFlash_.removeFirst();
    }
    persistReactionsToFlash();
}

void SessionController::persistReactionsToFlash()
{
    QJsonArray array;
    for (const QString& entry : reactionsToFlash_) {
        array.append(entry);
    }
    accountDb().putText("reactions-to-flash",
        QJsonDocument(array).toJson(QJsonDocument::Compact).toStdString());
}

QStringList SessionController::reactionsToFlash(const QString& e2eId) const
{
    if (activePeer_.isEmpty() || e2eId.isEmpty()
        || !reactionsToFlash_.contains(flashKey(activePeer_, e2eId))) {
        return {};
    }
    // Which of the emoji on this message to flash: the ones somebody else put
    // there. Our own, echoed from another device of ours, was never news.
    QStringList emoji;
    for (const Reaction& reaction : store_.reactionsFor(activePeer_, e2eId)) {
        if (reaction.reactor != fingerprint_ && !reaction.emoji.isEmpty()) {
            emoji << reaction.emoji;
        }
    }
    return emoji;
}

void SessionController::forgetReactionFlash()
{
    if (activePeer_.isEmpty()) {
        return;
    }
    const QString prefix = activePeer_ + "\n";
    const qsizetype before = reactionsToFlash_.size();
    reactionsToFlash_.removeIf(
        [&prefix](const QString& entry) { return entry.startsWith(prefix); });
    if (reactionsToFlash_.size() == before) {
        return;
    }
    persistReactionsToFlash();
    // The chips are what read this, and they re-read on this revision.
    ++reactionsRevision_;
    emit reactionsRevisionChanged();
}

void SessionController::react(const QString& e2eId, const QString& emoji)
{
    if (activePeer_.isEmpty() || e2eId.isEmpty()) {
        return;
    }
    // Toggle: tapping the emoji we already set removes our reaction.
    const QString next = (myReaction(e2eId) == emoji) ? QString() : emoji;
    // Setting one they reached for outside the standard set - typed, or tapped on
    // somebody else's chip - puts it in their recents. Removing one does not.
    if (!next.isEmpty() && !kStandardReactions.contains(next)) {
        rememberReaction(next);
    }
    store_.setReaction(activePeer_, e2eId, fingerprint_, next);
    emit requestSendReaction(activePeer_, e2eId, next);
    ++reactionsRevision_;
    emit reactionsRevisionChanged();
}

QString SessionController::myReaction(const QString& e2eId) const
{
    for (const Reaction& r : store_.reactionsFor(activePeer_, e2eId)) {
        if (r.reactor == fingerprint_) {
            return r.emoji;
        }
    }
    return {};
}

QVariantList SessionController::reactionSummary(const QString& e2eId) const
{
    QVariantList out;
    if (activePeer_.isEmpty() || e2eId.isEmpty()) {
        return out;
    }
    // Aggregate by emoji, preserving the order each emoji was first seen.
    QStringList order;
    QHash<QString, int> counts;
    QString mine;
    for (const Reaction& r : store_.reactionsFor(activePeer_, e2eId)) {
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
    qint64 sentAt = 0;
    QString e2eId;
    if (!conversation_.newestIncomingThrough(row, id, e2eId, sentAt)) {
        return;
    }
    // Nothing new has been read - scrolling within what is already read, or the
    // same row reported again. This is the first thing checked because the view
    // calls in on every pixel of movement, and it is what keeps the database out
    // of a scroll: the high-water is seeded from the stored mark when the
    // conversation opens, so a mark that does not advance has nothing to write.
    const qint64 prevAcked = lastReadAckedId_.value(activePeer_, 0);
    if (id <= prevAcked) {
        return;
    }
    lastReadAckedId_[activePeer_] = id;
    // Persist the read high-water and refresh the unread badge: the count drops as
    // messages genuinely scroll into the focused viewport. Monotonic, so re-reading
    // older history never lowers it.
    store_.setLastReadId(activePeer_, id);
    contacts_.setUnread(activePeer_, store_.unreadCount(activePeer_));
    // And the account's other devices, which hold the same conversation and have
    // no other way to learn it has been read. Held back rather than sent per
    // message: reading a long conversation advances this mark once per bubble,
    // and each send is an item in this account's own mailbox.
    pendingReadSync_[activePeer_] = sentAt;
    readSyncTimer_.start(kReadSyncIdleMs);
    // Sending a read receipt is opt-in (the "send read receipts" setting). The
    // unread high-water above is advanced regardless, so unread tracking always
    // works even with receipts disabled - and so does reading an account that is
    // switched off, which writes nothing at all.
    if (!sendReceipts_ || !online_) {
        return;
    }
    // A read sends a delivery receipt so the sender's bubble greens.
    emit requestSendReceipt(activePeer_, e2eId);
}

void SessionController::flushReadSync()
{
    readSyncTimer_.stop();
    if (!online_) {
        // Off is off: an account that is only being read tells its own other
        // devices nothing, and there is nothing here worth queueing until it is
        // switched on - the mark is already stored on this device.
        pendingReadSync_.clear();
        return;
    }
    for (auto it = pendingReadSync_.constBegin(); it != pendingReadSync_.constEnd(); ++it) {
        emit requestSyncRead(it.key(), it.value());
    }
    pendingReadSync_.clear();
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
    contactState_[peer].request = ContactState::eAnswered;
    ++contactsRevision_;
    emit contactsRevisionChanged();
}

void SessionController::onContactAddDone(const QString& opId, bool ok, const QString& status)
{
    emit requestForgetPendingAdd(opId);
    finishOperation(opId, ok, status);
    // The note in the chat carries the outcome and then stops being a progress
    // line: a failed add says why, right where the user was watching. Its state
    // settles with it, so the next open does not read it as still running.
    const auto row = contactProgressRows_.constFind(opId);
    if (row != contactProgressRows_.cend()) {
        store_.updateStatus(row.value(), DeliveryStatus::Received);
        if (!ok) {
            // No longer a progress line: it is an outcome with two ways out of
            // it, and the view draws those from the type.
            store_.setType(row.value(), QStringLiteral("contact.failed"));
            conversation_.setTypeForId(row.value(), QStringLiteral("contact.failed"));
        }
    }
    writeContactProgress(opId, ok ? status : QStringLiteral("Could not add: ") + status);
    contactProgressRows_.remove(opId);
}

void SessionController::onContactAlreadyKnown(const QString& opId, const QString& fingerprint)
{
    // Nothing was sent, so there is nothing to take up again on the next run.
    emit requestForgetPendingAdd(opId);
    finishOperation(opId, true, QStringLiteral("Already in your contacts"));
    contactProgressRows_.remove(opId);
    openConversation(fingerprint);
    emit actionOk(QStringLiteral("Already in your contacts"));
}

void SessionController::onContactRequestSent(
    const QString& fingerprint, const QString& intro, const QString& requestId)
{
    // Mirror the request on our own side: store the intro we just sent as an
    // outgoing message and open a chat for the new peer, so adding a contact
    // produces a visible conversation immediately instead of an empty chat-list
    // entry. The contact itself is already persisted by the core session; the
    // following sync() refresh will keep the chat list consistent.
    if (fingerprint.isEmpty()) {
        return;
    }
    // The same request sent again is the same request: it keeps its name on the
    // wire, so the note keeps it here too and the conversation holds one plate
    // rather than one per attempt.
    if (store_.oldestOfType(fingerprint, QStringLiteral("contact.request"), /*outgoing=*/true)
        != 0) {
        if (activePeer_ != fingerprint) {
            openConversation(fingerprint);
        }
        return;
    }
    const QString body = intro.isEmpty() ? QStringLiteral("Contact request sent.") : intro;
    StoredMessage m;
    m.peer = fingerprint;
    m.outgoing = true;
    m.type = "contact.request";
    m.e2eId = requestId.isEmpty() ? SessionController_genE2eId() : requestId;
    m.text = body;
    m.ts = nowMillis();
    m.orderKey = m.ts;
    // The request was delivered to the peer's server before this fires (the add
    // call returned without throwing), so it is honestly past our own server.
    m.status = DeliveryStatus::AtRecipientServer;
    m.id = store_.append(m);
    statusById_[m.id] = m.status;
    contacts_.touch(fingerprint, {}, body, m.ts, false);
    if (activePeer_ == fingerprint) {
        showInActiveView(m, true);
        return;
    }
    // An add by alias only learns who the peer is here, so this is where its chat
    // opens - with its own transcript loaded, and not with this line appended to
    // the one that happened to be open.
    openConversation(fingerprint);
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
    callEndedLocally_ = true;
    callTones_.stop();
    // Remembered until another call arrives: the sync thread may already have a
    // state update for this one on its way, and applying it after the refusal
    // put the window back on the desktop and the ringtone with it.
    refusedCallId_ = callId_;
    // Refused is over, here and now: the ringtone stops, the window goes and the
    // buttons come back at once, while the refusal itself travels in the
    // background. Waiting for it meant ringing at somebody who had already been
    // refused, for as long as I2P took to carry the word.
    const QString callId = callId_;
    onCallStateChanged(0, QString(), QString(), false, QString(), false, 0, 0.0F, 0.0F);
    emit requestDeclineCall(callId);
}

void SessionController::endCall()
{
    // Hanging up is not a call that failed: it ends the tones here rather than
    // letting the outcome speak for it.
    callEndedLocally_ = true;
    callTones_.stop();
    emit requestEndCall();
}

void SessionController::setCallMuted(const bool muted)
{
    emit requestSetCallMuted(muted);
}

void SessionController::onCallStateChanged(const int state, const QString& peer,
    const QString& callId, const bool muted, const QString& stage, const bool peerRinging,
    const qint64 connectedAtMs, const float inputLevel, const float outputLevel)
{
    // The levels move on every tick and nothing else does: they have their own
    // signal, so a level meter does not re-evaluate the whole call window.
    if (!qFuzzyCompare(static_cast<qreal>(callInputLevel_), static_cast<qreal>(inputLevel))
        || !qFuzzyCompare(static_cast<qreal>(callOutputLevel_), static_cast<qreal>(outputLevel))) {
        callInputLevel_ = inputLevel;
        callOutputLevel_ = outputLevel;
        emit callLevelsChanged();
    }
    static const char* const kNames[] = {"idle", "outgoing", "incoming", "active"};
    const QString name = (state >= 0 && state <= 3) ? QString::fromLatin1(kNames[state])
                                                    : QStringLiteral("idle");
    // A call the user refused is over here, whatever is still in flight about it.
    if (!refusedCallId_.isEmpty() && callId == refusedCallId_
        && name != QLatin1String("idle")) {
        return;
    }
    if (!callId.isEmpty() && callId != refusedCallId_) {
        refusedCallId_.clear();
    }
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

    // Call-progress tones: silence while the invitation is still travelling, a
    // ringback from the moment a device of theirs is showing the call until there
    // is a voice to hear.
    const bool waitingOnThem = (name == QLatin1String("outgoing") && peerRinging)
        || (name == QLatin1String("active") && connectedAtMs == 0);
    if (waitingOnThem) {
        callTones_.ringback();
    } else if (name == QLatin1String("idle")) {
        callTones_.endRingback();
        callEndedLocally_ = false;
    } else {
        callTones_.stop();
    }

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
    const QString& peer, const bool incoming, const int outcome, qint64 durationSec)
{
    (void)durationSec;  // nothing is written down, so its length is nobody's business
    if (peer.isEmpty()) {
        return;
    }
    // Outcome ints mirror Session::CallOutcome: 0 answered, 1 no-answer, 2 declined,
    // 3 missed, 4 cancelled, 5 busy, 6 refused (the peer takes no calls), 7 refused
    // here (this account takes none, or none from them).
    // A call of ours that did not happen says so out loud: the user is not
    // necessarily looking at the window when the far end refuses.
    if (!incoming && !callEndedLocally_
        && (outcome == static_cast<int>(Session::CallOutcome::eNoAnswer)
            || outcome == static_cast<int>(Session::CallOutcome::eDeclined)
            || outcome == static_cast<int>(Session::CallOutcome::eBusy)
            || outcome == static_cast<int>(Session::CallOutcome::eRefused))) {
        callTones_.failure();
    }
    // A call leaves nothing behind in the conversation: no line in it and no
    // preview in the chat list. A call is a thing that happened at the time it
    // happened - it is not correspondence, and a column of "Missed call" over a
    // chat says nothing the user did not already see the window say.
}

}  // namespace bazarish::app
