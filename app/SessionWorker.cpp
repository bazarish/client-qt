// Bazarish project (c) 2026
#include "SessionWorker.hpp"

#include "SessionShared.hpp"

#include "QtAudioIo.hpp"
#include "I2pRouter.hpp"
#include "FederationFetch.hpp"





#include "DeliveryStatus.hpp"
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
// A contact request names itself with this many random bytes, and keeps the name
// if the add has to be taken up again.
constexpr std::size_t kContactRequestIdBytes = 8;

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

// How often a live call is asked for its state. The moment media starts flowing
// is what both sides show as "in call", and the mailbox poll is far too coarse
// to carry it; the level meters are drawn from the same tick, which is what sets
// the rate - slower than this and a voice reads as a series of steps.
constexpr int kCallWatchIntervalMs = 100;

// How long the server is asked to hold the mail request open. Its own cap is
// lower; asking for more than it allows is answered sooner, which costs nothing.
// The request is answered the moment something lands, so this is only how often
// the loop asks again while nothing does.
constexpr int kEventWaitSeconds = 30;

// How often the local upkeep runs. It reads no mail: mail arrives by the wait
// above and by nothing else.
constexpr int kMaintenanceIntervalMs = 2000;

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

}  // namespace bazarish::app
