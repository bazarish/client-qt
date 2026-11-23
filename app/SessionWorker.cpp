// Bazarish project (c) 2026
#include "SessionWorker.hpp"
#include "Background.hpp"

#include "PathText.hpp"
#include "LinkFailureText.hpp"
#include "SessionShared.hpp"

#include "Documents.hpp"

#include "QtAudioIo.hpp"
#ifdef Q_OS_ANDROID
#include "AndroidCallAudio.hpp"
#endif
#include "I2pRouter.hpp"
#include "FederationFetch.hpp"

#include "DeliveryStatus.hpp"
#include "SystemNotes.hpp"
#include "DevicePairing.hpp"
#include "ServiceSchedule.hpp"
#include "Session.hpp"

#include <bazarish/Clock.hpp>
#include <bazarish/Crypto.hpp>
#include <bazarish/Descriptor.hpp>
#include <bazarish/Devices.hpp>
#include <bazarish/Errors.hpp>
#include <bazarish/I2p.hpp>
#include <bazarish/I2pAddress.hpp>
#include <bazarish/Limits.hpp>
#include <bazarish/Portal.hpp>

#pragma push_macro("emit")
#undef emit
#include <bazarish/Log.hpp>
#pragma pop_macro("emit")

#include <QByteArray>
#include <QDateTime>
#include <QEvent>
#include <QSet>
#include <chrono>
#include <QTimer>
#include <cstring>

#include <algorithm>
#include <array>
#include <ctime>
#include <exception>
#include <stdexcept>
#include <memory>
#include <mutex>
#include <optional>
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

namespace {
constexpr std::size_t kContactRequestIdBytes = 8;

QString noteForFetchStage(const bazarish::client::FetchStage stage)
{
    switch (stage) {
    case bazarish::client::FetchStage::eTakingDest:
        return encodeSystemNote(QT_TR_NOOP("choosing an address..."));
    case bazarish::client::FetchStage::eBuildingDest:
        return encodeSystemNote(QT_TR_NOOP("preparing an address..."));
    case bazarish::client::FetchStage::eReaching:
        return encodeSystemNote(QT_TR_NOOP("connecting over I2P..."));
    case bazarish::client::FetchStage::eWaiting:
        return encodeSystemNote(QT_TR_NOOP("waiting for an answer..."));
    }
    throw std::logic_error("no note for this contact-card fetch stage");
}

QString noteForCardStep(const bazarish::client::Session::CardStep step, const QString& alias,
    const QString& fetchStage)
{
    using Step = bazarish::client::Session::CardStep;
    switch (step) {
    case Step::eLookingUpAlias:
        return fetchStage.isEmpty()
            ? encodeSystemNote(QT_TR_NOOP("Looking up %1 in the alias registry..."), {alias})
            : encodeSystemNote(QT_TR_NOOP("Looking up %1 in the alias registry: %2"),
                {alias, fetchStage});
    case Step::eSolvingPuzzle:
        return encodeSystemNote(QT_TR_NOOP("The alias registry is busy: completing its check..."));
    case Step::eFetchingCard:
        return fetchStage.isEmpty()
            ? encodeSystemNote(QT_TR_NOOP("Fetching the contact card..."))
            : encodeSystemNote(QT_TR_NOOP("Fetching the contact card: %1"), {fetchStage});
    }
    throw std::logic_error("no note for this contact-add step");
}

QString noteForCallStage(const bazarish::client::Session::CallStage stage)
{
    using bazarish::client::Session;
    switch (stage) {
    case Session::CallStage::eNone:
        return QString();
    case Session::CallStage::ePreparing:
        return encodeSystemNote(QT_TR_NOOP("Preparing your call address"));
    case Session::CallStage::eDelivering:
        return encodeSystemNote(QT_TR_NOOP("Delivering the invitation"));
    case Session::CallStage::eDelivered:
        return encodeSystemNote(QT_TR_NOOP("Invitation delivered"));
    case Session::CallStage::eRinging:
        return encodeSystemNote(QT_TR_NOOP("Their device is ringing"));
    case Session::CallStage::eOpening:
        return encodeSystemNote(QT_TR_NOOP("Opening the audio path"));
    }
    throw std::logic_error("no note for this call stage");
}

QString textForTransferStage(const bazarish::client::TransferStage stage)
{
    using bazarish::client::TransferStage;
    switch (stage) {
    case TransferStage::eNone:
        return QString();
    case TransferStage::eAsking:
        return QObject::tr("Asking the sender...");
    case TransferStage::eEncrypting:
        return QObject::tr("Encrypting...");
    case TransferStage::eMakingAddress:
        return QObject::tr("Making an address");
    case TransferStage::ePublishing:
        return QObject::tr("Publishing the address");
    case TransferStage::eWaiting:
        return QObject::tr("Waiting for the recipient...");
    case TransferStage::eSending:
        return QObject::tr("Sending...");
    case TransferStage::eConnecting:
        return QObject::tr("Connecting...");
    case TransferStage::eReceiving:
        return QObject::tr("Receiving...");
    }
    throw std::logic_error("no text for this transfer stage");
}

class WorkerOp {
public:
    WorkerOp(SessionWorker& worker, const QString& id, const QString& kind,
        const QString& title, const QString& status)
        : worker_(worker)
        , id_(id)
    {
        emit worker_.opBegin(id_, kind, title, status);
    }
    WorkerOp(const WorkerOp&) = delete;
    WorkerOp& operator=(const WorkerOp&) = delete;
    ~WorkerOp() { emit worker_.opDone(id_, ok_, status_); }

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
    SessionWorker& worker_;
    const QString id_;
    bool ok_ = false;
    QString status_ = SessionWorker::tr("Failed");
};

constexpr std::chrono::milliseconds kSlowPass{400};

constexpr std::chrono::milliseconds kSlowStretch{200};

constexpr int kFirstClientError = 400;
constexpr int kFirstServerError = 500;

constexpr qint64 kTransientRenewLeadSeconds = 5 * 24 * 3600;

constexpr qint64 kTransientJitterSeconds = 6 * 3600;

constexpr int kCallWatchIntervalMs = 100;

QVariantMap wireLine(const bazarish::client::WireEvent& event)
{
    return {
        {QStringLiteral("at"), QVariant::fromValue(event.atMillis)},
        {QStringLiteral("outgoing"), event.outgoing},
        {QStringLiteral("what"), QString::fromStdString(event.what)},
        {QStringLiteral("status"), QString::fromStdString(event.status)},
        {QStringLiteral("detail"), QString::fromStdString(event.detail)},
    };
}

bazarish::DeviceRole roleOf(const bool primary)
{
    return primary ? bazarish::DeviceRole::ePrimary : bazarish::DeviceRole::eSecondary;
}

QVariantList aliasHoldingRows(const std::vector<bazarish::client::Session::AliasHolding>& held)
{
    QVariantList rows;
    for (const bazarish::client::Session::AliasHolding& holding : held) {
        if (!holding.inApp) {
            continue;
        }
        QVariantMap row;
        row[QStringLiteral("alias")] = QString::fromStdString(holding.alias);
        row[QStringLiteral("word")] = QString::fromStdString(holding.word);
        row[QStringLiteral("resolving")] = holding.bindingWanted;
        row[QStringLiteral("live")] = holding.bindingWanted && holding.bound;
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
    return SessionWorker::tr("Your deposit will not cover the next renewal");
}

// The recipient's server answers in its own English; what it means is said in the app's language.
QString refusalText(const bazarish::client::OutboundCourier::Outcome& outcome)
{
    if (outcome.reason != bazarish::client::NotStored::eRefused) {
        return notStoredText(outcome.reason);
    }
    const std::optional<bazarish::ErrorCode> code
        = bazarish::errorCodeFromString(outcome.errorCode);
    if (code == bazarish::ErrorCode::eStorageFull) {
        return SessionWorker::tr("The recipient's mailbox is full");
    }
    if (code == bazarish::ErrorCode::eMessageTooLarge) {
        return SessionWorker::tr("The message is too large for the recipient's server");
    }
    if (code == bazarish::ErrorCode::eContactRateLimited) {
        return SessionWorker::tr("Too many messages to this contact this minute");
    }
    return SessionWorker::tr("The recipient's server refused this message");
}

QString contactRefusalText(const bazarish::client::OutboundCourier::Outcome& outcome)
{
    if (outcome.reason != bazarish::client::NotStored::eRefused) {
        return notStoredText(outcome.reason);
    }
    if (bazarish::errorCodeFromString(outcome.errorCode)
        == bazarish::ErrorCode::eContactRequestTooLarge) {
        return SessionWorker::tr("The contact request is too long");
    }
    return SessionWorker::tr("The contact's server refused the request");
}

FarSide cardStepSide(const bazarish::client::Session::CardStep step)
{
    using Step = bazarish::client::Session::CardStep;
    switch (step) {
    case Step::eLookingUpAlias:
    case Step::eSolvingPuzzle:
        return FarSide::eRegistry;
    case Step::eFetchingCard:
        return FarSide::eContactServer;
    }
    throw std::logic_error("no far side for this card step");
}

bazarish::client::DeliveryWatch watchFor(SessionWorker& owner, const qint64 localId)
{
    bazarish::client::DeliveryWatch watch;
    const QPointer<SessionWorker> worker(&owner);
    watch.onPhase = [worker, localId](const std::string& phase) {
        if (worker.isNull()) {
            return;
        }
        if (phase == bazarish::client::kPhaseDialing) {
            emit worker->sendProgress(localId, DeliveryStatus::Delivering);
        }
        emit worker->sendPhase(localId, QString::fromStdString(phase));
    };
    watch.onOutcome
        = [worker, localId](const bazarish::client::OutboundCourier::Outcome& outcome) {
              if (worker.isNull()) {
                  return;
              }
              QMetaObject::invokeMethod(worker, "refreshContacts", Qt::QueuedConnection);
              if (outcome.stored) {
                  emit worker->sendProgress(localId, DeliveryStatus::AtRecipientServer);
                  emit worker->sendResult(localId, true, {});
                  return;
              }
              emit worker->sendResult(localId, false, refusalText(outcome));
          };
    return watch;
}

QVariantMap overviewMap(const bazarish::client::AccountOverview& overview)
{
    QVariantList devices;
    for (const bazarish::client::OverviewDevice& device : overview.devices) {
        devices.append(QVariantMap{
            {QStringLiteral("deviceId"), QString::fromStdString(device.device)},
            {QStringLiteral("current"), device.current},
            {QStringLiteral("queue"), static_cast<qulonglong>(device.queued)},
        });
    }
    return {
        {QStringLiteral("known"), !overview.version.empty()},
        {QStringLiteral("dest"), QString::fromStdString(overview.dest)},
        {QStringLiteral("state"), QString::fromStdString(overview.state)},
        {QStringLiteral("transientExpires"), static_cast<qint64>(overview.transientExpires)},
        {QStringLiteral("usedBytes"), static_cast<qulonglong>(overview.usage.usedBytes)},
        {QStringLiteral("quotaBytes"), static_cast<qulonglong>(overview.usage.quotaBytes)},
        {QStringLiteral("devices"), devices},
    };
}

}  // namespace

SessionWorker::~SessionWorker()
{
    stopEventWaiter();
    stopOverview();
    stopApprovalWaiter();
    stopErrands();
}

bool SessionWorker::event(QEvent* event)
{
    const bool handled = QObject::event(event);
    if (event->type() == QEvent::MetaCall) {
        sessionDutiesFailed_ = false;
        armSchedule();
    }
    return handled;
}

void SessionWorker::startMaintenance()
{
    if (maintenanceTimer_ == nullptr) {
        maintenanceTimer_ = new QTimer(this);
        maintenanceTimer_->setSingleShot(true);
        connect(maintenanceTimer_, &QTimer::timeout, this, &SessionWorker::maintain);
    }
    maintaining_ = true;
    armSchedule();
}

void SessionWorker::stopMaintenance()
{
    maintaining_ = false;
    if (maintenanceTimer_ != nullptr) {
        maintenanceTimer_->stop();
    }
}

std::optional<qint64> SessionWorker::transientCheckAtMs(const qint64 nowMs) const
{
    if (!session_ || !session_->isConnected()) {
        return std::nullopt;
    }
    return bazarish::client::cadencedAtMs(nowMs, lastTransientCheckMs_);
}

std::optional<qint64> SessionWorker::aliasAttemptAtMs() const
{
    if (!session_ || !session_->isConnected() || aliasErrandRunning_) {
        return std::nullopt;
    }
    return bazarish::client::cadencedAtMs(
        bazarish::client::secondToMs(session_->aliasServiceDueAt()), lastAliasServiceMs_);
}

void SessionWorker::armSchedule()
{
    if (!session_ || !maintaining_ || maintenanceTimer_ == nullptr) {
        return;
    }
    const qint64 now = nowMillis();
    const std::optional<qint64> sessionAt = sessionDutiesFailed_
        ? std::nullopt
        : bazarish::client::atMsAfter(now, session_->untilNextDue());
    const std::optional<std::chrono::milliseconds> wait = bazarish::client::waitForSoonest(
        now, {sessionAt, transientCheckAtMs(now), aliasAttemptAtMs()});
    if (!wait.has_value()) {
        maintenanceTimer_->stop();
        return;
    }
    maintenanceTimer_->start(*wait);
}

void SessionWorker::serviceAliasesIfDue()
{
    if (session_ && session_->isConnected() && session_->aliasServicingDue()) {
        startAliasErrand(/*byHand=*/false);
    }
}

void SessionWorker::startReceiving()
{
    startMaintenance();
    if (session_->approvalState().pending) {
        startApprovalWaiter();
    } else {
        startEventWaiter();
    }
    if (overviewOpen_) {
        watchOverview(true);
    }
}

void SessionWorker::startErrands()
{
    if (errands_.joinable() || !session_) {
        return;
    }
    bazarish::client::Session::ContactFetchContext context;
    try {
        context = session_->contactFetchContext();
    } catch (const std::exception& error) {
        bazarish::log::warn("no errands: {}", error.what());
        return;
    }
    errandsRunning_ = true;
    errands_ = std::thread([this, context]() {
        const std::unique_ptr<bazarish::client::Client> errands
            = bazarish::client::Session::makeEventClient(context);
        while (true) {
            std::vector<std::string> acks;
            bool releaseOverview = false;
            {
                std::unique_lock<std::mutex> lock(errandMutex_);
                errandWake_.wait(
                    lock, [this]() { return !errandsRunning_ || !errandQueue_.empty(); });
                if (!errandsRunning_ && errandQueue_.empty()) {
                    return;
                }
                if (errandQueue_.front().releaseOverview) {
                    releaseOverview = true;
                    errandQueue_.pop_front();
                }
                while (!releaseOverview && !errandQueue_.empty()
                    && !errandQueue_.front().releaseOverview
                    && acks.size() < bazarish::kMaxAckIdsPerRequest) {
                    acks.push_back(std::move(errandQueue_.front().pendingId));
                    errandQueue_.pop_front();
                }
            }
            if (releaseOverview) {
                try {
                    (void)errands->overview();
                } catch (const std::exception& error) {
                    bazarish::log::info("the held overview was not released: {}", error.what());
                }
                continue;
            }
            try {
                errands->ack(acks);
            } catch (const std::exception& error) {
                bazarish::log::warn(
                    "pending items not given back, owed to the next mail wait: {}", error.what());
                const std::lock_guard<std::mutex> lock(drainMutex_);
                owedAcks_.insert(owedAcks_.end(), acks.begin(), acks.end());
            }
            QMetaObject::invokeMethod(
                this,
                [this, released = std::move(acks)]() {
                    if (session_) {
                        for (const std::string& pendingId : released) {
                            session_->forgetPending(pendingId);
                        }
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
    mailGate_.restart();
    eventWaiter_ = std::thread([this, context, running = eventWaiterRunning_]() {
        std::shared_ptr<bazarish::client::Client> waiter;
        // A held wait answers late by design: until the server has answered, it is not held.
        bool answered = false;
        // What the first answer after coming online lists is the backlog.
        bool caughtUp = false;
        bool askedAfterRegistering = false;
        while (running->load()) {
            {
                // A list taken before the last pass is acked would name what it already took in.
                std::unique_lock<std::mutex> lock(drainMutex_);
                drainDone_.wait(
                    lock, [this, &running]() { return drainSettled_ || !running->load(); });
            }
            mailGate_.asking();
            try {
                if (!waiter) {
                    waiter = bazarish::client::Session::makeEventClient(context);
                    waiter->onLinkStage([this](const bazarish::client::LinkStage stage) {
                        QMetaObject::invokeMethod(
                            this, [this, stage]() { emit syncLinkStage(stage); },
                            Qt::QueuedConnection);
                    });
                    const std::lock_guard<std::mutex> lock(waiterClientMutex_);
                    if (!running->load()) {
                        return;
                    }
                    waiterClient_ = waiter;
                }
                std::vector<std::string> owed;
                {
                    const std::lock_guard<std::mutex> lock(drainMutex_);
                    owed = owedAcks_;
                }
                if (!owed.empty()) {
                    waiter->ack(owed);
                    const std::lock_guard<std::mutex> lock(drainMutex_);
                    owedAcks_.erase(owedAcks_.begin(),
                        owedAcks_.begin() + static_cast<std::ptrdiff_t>(owed.size()));
                }
                const std::vector<bazarish::client::PendingEntry> waiting
                    = bazarish::client::Session::waitForMail(*waiter,
                        answered ? bazarish::client::Client::MailWait::eHeld
                                 : bazarish::client::Client::MailWait::eAtOnce,
                        *context.keptMail, *context.devices);
                if (!running->load()) {
                    return;
                }
                if (!answered) {
                    bazarish::log::info("the server answered the mail wait");
                }
                answered = true;
                askedAfterRegistering = false;
                mailGate_.answered();
                const std::optional<bazarish::client::MailboxUsage> usage
                    = waiter->lastMailboxUsage();
                QMetaObject::invokeMethod(
                    this,
                    [this, usage]() {
                        if (usage.has_value()) {
                            takeMailboxUsage(*usage);
                        }
                        if (overview_) {
                            overview_->linkBack();
                        }
                    },
                    Qt::QueuedConnection);
                QMetaObject::invokeMethod(
                    this,
                    [this]() {
                        emit syncReachable(true, {}, bazarish::client::LinkFailure::eNone);
                    },
                    Qt::QueuedConnection);
                streamMailbox(*waiter, waiting, !caughtUp, *running);
                caughtUp = true;
            } catch (const std::exception& error) {
                if (!running->load()) {
                    return;
                }
                answered = false;
                caughtUp = false;
                if (const auto* apiError = dynamic_cast<const bazarish::client::ApiError*>(&error);
                    apiError != nullptr
                    && apiError->code == bazarish::ErrorCode::eClientUnregistered) {
                    if (askedAfterRegistering) {
                        bazarish::log::warn(
                            "the server forgot this device right after registering it");
                    } else {
                        QMetaObject::invokeMethod(
                            this, [this]() { registerThisDevice(); }, Qt::QueuedConnection);
                    }
                }
                if (const auto* apiError = dynamic_cast<const bazarish::client::ApiError*>(&error);
                    apiError != nullptr && apiError->code == bazarish::ErrorCode::eAccountUnknown
                    && !askedAfterRegistering) {
                    QMetaObject::invokeMethod(
                        this, [this]() { finishRegistration(); }, Qt::QueuedConnection);
                }
                const QString refusal
                    = waiter ? QString::fromStdString(waiter->gatewayRefusal()) : QString();
                const bool serverDown = waiter && waiter->serverBehindFacadeDown();
                const bazarish::client::LinkFailure failure
                    = waiter ? waiter->linkFailure() : bazarish::client::LinkFailure::eNone;
                QMetaObject::invokeMethod(
                    this,
                    [this, refusal, failure]() { emit syncReachable(false, refusal, failure); },
                    Qt::QueuedConnection);
                bazarish::log::info("mail wait failed ({}): {}",
                    bazarish::client::linkFailureName(failure), error.what());
                const bazarish::client::MailWaitCause cause = serverDown
                    ? mailGate_.awaitServer()
                    : mailGate_.awaitCause(
                        [&waiter]() { return waiter && waiter->facadePublishedAnew(); });
                if (cause == bazarish::client::MailWaitCause::eStopped) {
                    return;
                }
                askedAfterRegistering = cause == bazarish::client::MailWaitCause::eRegistered;
                bazarish::log::info("asking for mail again: {}",
                    bazarish::client::mailWaitCauseName(cause));
                if (waiter && cause != bazarish::client::MailWaitCause::eStaleLeaseSet) {
                    waiter->liftStandOff();
                }
            }
        }
    });
}

void SessionWorker::streamMailbox(bazarish::client::Client& waiter,
    const std::vector<bazarish::client::PendingEntry>& waiting, const bool catchUp,
    const std::atomic<bool>& running)
{
    std::uint64_t totalBytes = 0;
    for (const bazarish::client::PendingEntry& entry : waiting) {
        totalBytes += entry.bytes;
    }
    if (!waiting.empty()) {
        const std::lock_guard<std::mutex> lock(drainMutex_);
        drainSettled_ = false;
        streamEnded_ = false;
    }
    QMetaObject::invokeMethod(
        this,
        [this, catchUp, totalBytes, items = waiting.size()]() {
            beginStream(catchUp, totalBytes, items);
        },
        Qt::QueuedConnection);
    if (waiting.empty()) {
        return;
    }
    const auto endStream = [this]() {
        {
            const std::lock_guard<std::mutex> lock(drainMutex_);
            streamEnded_ = true;
        }
        QMetaObject::invokeMethod(this, "sync", Qt::QueuedConnection);
    };
    // The next batch is on its way while the worker takes in the one before it.
    try {
        for (std::size_t begin = 0; begin < waiting.size();) {
            const std::size_t end = bazarish::client::fetchBatchEnd(waiting, begin);
            std::vector<bazarish::client::Session::MailboxItem> batch
                = bazarish::client::Session::fetchMailbox(waiter,
                    std::vector<bazarish::client::PendingEntry>(
                        waiting.begin() + static_cast<std::ptrdiff_t>(begin),
                        waiting.begin() + static_cast<std::ptrdiff_t>(end)));
            {
                std::unique_lock<std::mutex> lock(drainMutex_);
                drainDone_.wait(
                    lock, [this, &running]() { return streamed_.empty() || !running.load(); });
                if (!running.load()) {
                    return;
                }
                for (bazarish::client::Session::MailboxItem& item : batch) {
                    streamed_.push_back(std::move(item));
                }
            }
            QMetaObject::invokeMethod(this, "sync", Qt::QueuedConnection);
            begin = end;
        }
    } catch (const std::exception&) {
        endStream();
        throw;
    }
    endStream();
    std::unique_lock<std::mutex> lock(drainMutex_);
    drainDone_.wait(lock, [this, &running]() { return drainSettled_ || !running.load(); });
}

void SessionWorker::beginStream(
    const bool catchUp, const std::uint64_t totalBytes, const std::size_t items)
{
    streamBytes_ = totalBytes;
    takenBytes_ = 0;
    if (!catchUp) {
        return;
    }
    catchingUp_ = items > 0;
    syncShown_ = items > Session::kPendingItemsPerPass;
    emit syncProgress(catchingUp_, syncShown_, 0, static_cast<qint64>(totalBytes));
}

void SessionWorker::registerThisDevice()
{
    if (!session_ || !session_->isConnected()) {
        return;
    }
    bazarish::log::info("this device is not registered with the server: registering it");
    registerAgain();
}

void SessionWorker::registerAgain()
{
    try {
        session_->registerAccount();
    } catch (const std::exception& error) {
        bazarish::log::warn("this device could not register again: {}", error.what());
        return;
    }
    emitI2pKeyState();
    mailGate_.registered();
    serviceAliasesIfDue();
}

void SessionWorker::finishRegistration()
{
    if (!session_ || !session_->isConnected() || !session_->registrationUnfinished()) {
        return;
    }
    bazarish::log::info("this account's registration did not finish: registering it");
    registerAgain();
}

void SessionWorker::stopEventWaiter()
{
    if (eventWaiterRunning_) {
        const std::lock_guard<std::mutex> lock(drainMutex_);
        eventWaiterRunning_->store(false);
    }
    drainDone_.notify_all();
    mailGate_.stop();
    {
        const std::lock_guard<std::mutex> lock(waiterClientMutex_);
        if (waiterClient_) {
            waiterClient_->close();
        }
    }
    if (session_) {
        session_->releaseI2pLinks();
    }
    if (eventWaiter_.joinable()) {
        eventWaiter_.join();
    }
    eventWaiter_ = std::thread();
    eventWaiterRunning_.reset();
    {
        const std::lock_guard<std::mutex> lock(waiterClientMutex_);
        waiterClient_.reset();
    }
    const std::lock_guard<std::mutex> lock(drainMutex_);
    streamed_.clear();
    streamEnded_ = true;
    drainSettled_ = true;
}

void SessionWorker::settleDrain()
{
    {
        const std::lock_guard<std::mutex> lock(drainMutex_);
        if (!streamed_.empty() || !streamEnded_
            || (session_ != nullptr && session_->awaitingAcks() > 0)) {
            return;
        }
        drainSettled_ = true;
    }
    drainDone_.notify_all();
}

void SessionWorker::openAccount(
    const QString& dir, const QString& passphrase, const bool startOnline)
{
    try {
        if (Session::storedKind(toPath(dir), passphrase.toStdString())
            != bazarish::AccountKind::eUser) {
            emit openFailed(tr("This account was created as a bot or a group and cannot be opened here"));
            return;
        }
        std::unique_ptr<Session> opened = Session::open(toPath(dir), passphrase.toStdString());
        const std::lock_guard<std::mutex> lock(sessionLife_);
        session_ = std::move(opened);
    } catch (const std::exception& e) {
        emit openFailed(QString::fromUtf8(e.what()));
        return;
    }
    {
        const std::lock_guard<std::mutex> lock(sessionLife_);
        session_->startPresence(
            [this](const std::string& peer, const bazarish::client::PresenceState& state) {
                emit peerPresence(QString::fromStdString(peer), state.online, state.typing);
            });
    }
    session_->setFileSource(deviceFiles());
#ifdef Q_OS_ANDROID
    // A phone speaks a call as a phone call: the voice stream and the earpiece.
    using CallSource = AndroidCallSource;
    using CallSink = AndroidCallSink;
#else
    using CallSource = QtAudioSource;
    using CallSink = QtAudioSink;
#endif
    session_->setAudioBackend(
        []() -> std::unique_ptr<bazarish::AudioSource> { return std::make_unique<CallSource>(); },
        []() -> std::unique_ptr<bazarish::AudioSink> { return std::make_unique<CallSink>(); });
    session_->onServerAnswered([this]() {
        mailGate_.serverAnswered();
        approvalGate_.serverAnswered();
        emit syncReachable(true, {}, bazarish::client::LinkFailure::eNone);
    });
    session_->openOutboxWhenAnswered(mailGate_);
    session_->onUnreadableMail([this]() {
        emit opDone(beginOp(tr("A message could not be read"), {}), false, tr("Discarded"));
    });
    session_->setAckSink([this](const std::string& pendingId) { queueAck(pendingId); });
    const QPointer<SessionWorker> self(this);
    session_->onCourierNews(
        [self]() { QMetaObject::invokeMethod(self, "takeCourierNews", Qt::QueuedConnection); });
    session_->onOwnDevicesChanged(
        [this]() { QMetaObject::invokeMethod(this, "emitDeviceState", Qt::QueuedConnection); });
    if (session_->isPrimary()) {
        emit loginSignerReady(session_->loginSigner());
    } else {
        emit loginSignerRefused();
    }
    emitKeptOverview();
    session_->onAddressDecision([this](const std::string& served, const std::string& ours) {
        emit addressMismatch(QString::fromStdString(served), QString::fromStdString(ours));
    });
    session_->setRoutingFanoutSink([this](const std::string& peer, const bool delivered) {
        emit routingTold(QString::fromStdString(peer), delivered);
    });
    const bool connected = session_->isConnected();
    emit opened(QString::fromStdString(session_->fingerprint()),
        QString::fromStdString(session_->displayName()), connected);
    toldDeviceState_.clear();
    emitDeviceState();
    armDayTimer();
    emit accountSettings(
        session_->acceptCalls(), session_->sendReceipts(), session_->showPresence());
    {
        const QVariantList rows = aliasHoldingRows(session_->aliasNames());
        if (!rows.isEmpty()) {
            emit aliasHoldings(rows, aliasHoldingsNote(rows, session_->aliasDepositCovers()));
        }
    }
    emitContacts();
    addsResumed_ = false;
    {
        const bazarish::Bytes& own = session_->avatar();
        if (!own.empty()) {
            emit avatarReady(QString::fromStdString(session_->fingerprint()), toQByteArray(own));
        }
        for (const std::string& fp : session_->contactFingerprints()) {
            const bazarish::Bytes av = session_->contactAvatar(fp);
            if (!av.empty()) {
                emit avatarReady(QString::fromStdString(fp), toQByteArray(av));
            }
        }
    }
    emitFacadeInfo();
    session_->setTransferHandler([this](const bazarish::client::TransferEvent& event) {
        const QString id = QString::fromStdString(event.e2eId);
        const QString peer = QString::fromStdString(event.peer);
        if (event.stage != bazarish::client::TransferStage::eNone) {
            emit transferStage(peer, id, textForTransferStage(event.stage));
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
            bazarish::log::info("file transfer failed: {}", event.error);
            emit servedFinished(peer, id, false, causeText(event.cause, FarSide::eContact));
            break;
        case bazarish::client::TransferState::eRequested:
            break;
        }
    });
    emitInvite();
    emitI2pKeyState();
    session_->onDeviceRevoked([this]() {
        QMetaObject::invokeMethod(this, "deviceRevoked", Qt::QueuedConnection);
    });
    session_->setSwitchedOff(!startOnline);
    const bool online = !session_->switchedOff();
    if (online) {
        startMaintenance();
    }
    if (connected && online) {
        if (!session_->listedByServer()) {
            bazarish::log::info("this device is not listed by its server yet: registering it");
            registerAgain();
        }
        startReceiving();
        sync();
    }
}

void SessionWorker::deviceRevoked()
{
    if (!session_) {
        return;
    }
    session_->noteDeviceRevoked();
    setSyncEnabled(false);
    emitDeviceState();
}

void SessionWorker::emitDeviceState()
{
    if (!session_) {
        return;
    }
    QSet<QString> asked;
    for (const bazarish::client::OwnDevice& device : session_->devicesToReview()) {
        asked.insert(QString::fromStdString(device.id));
    }
    QVariantList own;
    for (const bazarish::client::OwnDevice& device : session_->ownDevices()) {
        const QString id = QString::fromStdString(device.id);
        own.append(QVariantMap{
            {QStringLiteral("deviceId"), id},
            {QStringLiteral("primary"), device.role == bazarish::DeviceRole::ePrimary},
            {QStringLiteral("current"), device.current},
            {QStringLiteral("revoked"), device.revoked},
            {QStringLiteral("registered"), device.registered},
            {QStringLiteral("departed"), device.departed},
            {QStringLiteral("issuedAt"), static_cast<qint64>(device.issuedAt)},
            {QStringLiteral("review"), asked.contains(id)},
        });
    }
    const QString deviceId = QString::fromStdString(session_->deviceId());
    const bool primary = session_->isPrimary();
    const qint64 reserveUntil
        = QDateTime::currentSecsSinceEpoch() + session_->ownEpochReserveSeconds();
    const bool reserveLow = session_->ownEpochReserveLow();
    const bool revoked = session_->deviceRevoked();
    const QVariantList told{
        deviceId, primary, own, reserveUntil / bazarish::kSecondsPerDay, reserveLow, revoked};
    if (told == toldDeviceState_) {
        return;
    }
    toldDeviceState_ = told;
    emit deviceState(deviceId, primary, own, reserveUntil, reserveLow, revoked);
}

void SessionWorker::armDayTimer()
{
    if (dayTimer_ == nullptr) {
        dayTimer_ = new QTimer(this);
        dayTimer_->setSingleShot(true);
        connect(dayTimer_, &QTimer::timeout, this, [this]() {
            emitDeviceState();
            armDayTimer();
        });
    }
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    const qint64 untilNextDay = bazarish::kSecondsPerDay - now % bazarish::kSecondsPerDay;
    dayTimer_->start(std::chrono::seconds(untilNextDay));
}

void SessionWorker::readContactDevices(const QString& peer)
{
    withSession([this, &peer]() {
        QVariantList devices;
        for (const bazarish::client::ContactDevice& device :
            session_->contactDevices(peer.toStdString())) {
            if (!device.revoked) {
                devices.append(QVariantMap{{QStringLiteral("deviceId"), QString::fromStdString(device.id)}});
            }
        }
        emit contactDevices(peer, devices);
    });
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
        try {
            contact.invite = QString::fromStdString(session_->contactInviteUri(fp));
        } catch (const std::exception& error) {
            bazarish::log::debug("contact {} is not shareable yet: {}",
                bazarish::log::redact(fp), error.what());
        }
        contact.writable = session_->canWriteTo(fp);
        contact.notifications = session_->contactNotifications(fp);
        contact.calls = session_->contactCalls(fp);
        contact.kind = QString::fromUtf8(bazarish::accountKindName(session_->contactKind(fp)));
        contacts.push_back(contact);
    }
    QStringList blocked;
    for (const std::string& fp : session_->blockedPeers()) {
        blocked << QString::fromStdString(fp);
    }
    if (toldContacts_.has_value() && toldContacts_->first == contacts
        && toldContacts_->second == blocked) {
        return;
    }
    toldContacts_.emplace(contacts, blocked);
    emit contactsRefreshed(contacts, blocked);
}

void SessionWorker::emitFacadeInfo()
{
    if (!session_) {
        return;
    }
    QStringList configured;
    for (const std::string& address : session_->facadeAddresses()) {
        configured << QString::fromStdString(address);
    }
    QStringList reseeds;
    for (const std::string& url : session_->endpoint().reseeds) {
        reseeds << QString::fromStdString(url);
    }
    emit facadeInfo(QString::fromStdString(session_->activeFacadeAddress()), configured,
        QString::fromStdString(session_->endpoint().serverFingerprint), reseeds);
}

void SessionWorker::connectAndRegister(const QStringList& facades, const QString& serverFp,
    const QStringList& reseedUrls)
{
    if (!session_) {
        emit actionFailed(tr("no account is open"));
        return;
    }
    const std::string progressAccount = session_->fingerprint();
    try {
        emit connectProgress(5, tr("Preparing"));
        bazarish::client::setConnectProgressSink(progressAccount,
            [this](const bazarish::client::ConnectMilestone milestone) {
                emit connectProgress(bazarish::client::connectPercent(milestone),
                    milestoneText(milestone));
            });
        ServerEndpoint endpoint;
        endpoint.serverFingerprint = serverFp.toStdString();
        for (const QString& address : facades) {
            const QString trimmed = address.trimmed();
            if (!trimmed.isEmpty()) {
                endpoint.facades.push_back(
                    bazarish::client::parseFacadeAddress(trimmed.toStdString()));
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
            throw std::runtime_error(tr("Enter at least one facade address").toStdString());
        }
        stopEventWaiter();
        stopOverview();
        stopApprovalWaiter();
        stopErrands();
        session_->connectServer(endpoint);
        emitFacadeInfo();
        // Registering publishes this account's card, which a switched-off account must not do.
        if (session_->switchedOff()) {
            startReceiving();
            bazarish::client::setConnectProgressSink(progressAccount, {});
            emit actionFailed(tr("this account is switched off: the server connection is saved, "
                                 "switch the account on to connect"));
            return;
        }
        const bool overI2p = std::any_of(endpoint.facades.begin(), endpoint.facades.end(),
            [](const bazarish::client::Facade& f) {
                return bazarish::isB32I2pHost(f.host);
            });
        emit connectProgress(overI2p ? 8 : 20,
            overI2p ? tr("Connecting over I2P — the first connection takes minutes")
                    : tr("Connecting to the server"));
        session_->registerAccount();
    } catch (const std::exception& e) {
        startReceiving();
        bazarish::client::setConnectProgressSink(progressAccount, {});
        bazarish::log::warn("connecting failed: {}", e.what());
        const QString reason = failureText(e, FarSide::eServer);
        emit connectionChanged(false, reason);
        emit actionFailed(reason);
        return;
    }
    emit connectProgress(100, tr("Connected"));
    bazarish::client::setConnectProgressSink(progressAccount, {});
    emit connectionChanged(true, "active");
    emitI2pKeyState();
    emitContacts();
    emitDeviceState();
    serviceAliasesIfDue();
    emit actionOk(tr("Connected"));
    emitFacadeInfo();
    startReceiving();
    sync();
}

void SessionWorker::setSyncEnabled(bool on)
{
    if (session_) {
        session_->setSwitchedOff(!on);
        on = !session_->switchedOff();
    }
    bazarish::log::info("account switched {}", on ? "on" : "off");
    if (on) {
        startReceiving();
        sync();
    } else {
        stopEventWaiter();
        stopOverview();
        stopApprovalWaiter();
        stopMaintenance();
    }
}

void SessionWorker::rebuildI2pLinks()
{
    stopEventWaiter();
    if (!maintaining_) {
        return;
    }
    QMetaObject::invokeMethod(this, [this]() { emit linksRebuilding(); }, Qt::QueuedConnection);
    startEventWaiter();
    if (!eventWaiter_.joinable()) {
        QMetaObject::invokeMethod(
            this,
            [this]() { emit syncReachable(false, {}, bazarish::client::LinkFailure::eNone); },
            Qt::QueuedConnection);
    }
}

void SessionWorker::sync()
{
    drainMailbox();
    settleDrain();
    emitInvite();
}

void SessionWorker::drainMailbox()
{
    if (!session_ || !session_->isConnected()) {
        return;
    }
    std::vector<Session::MailboxItem> chunk;
    bool more = false;
    bool ended = false;
    {
        const std::lock_guard<std::mutex> lock(drainMutex_);
        while (!streamed_.empty() && chunk.size() < Session::kPendingItemsPerPass) {
            chunk.push_back(std::move(streamed_.front()));
            streamed_.pop_front();
        }
        more = !streamed_.empty();
        ended = !more && streamEnded_;
    }
    drainDone_.notify_all();
    std::uint64_t chunkBytes = 0;
    for (const Session::MailboxItem& item : chunk) {
        chunkBytes += item.entry.bytes;
    }
    const bazarish::log::Slow timed("reading the mailbox", kSlowPass);
    std::vector<IncomingMessage> messages;
    try {
        messages = session_->takeIn(std::move(chunk), false);
    } catch (const std::exception& error) {
        stopEventWaiter();
        emit actionFailed(tr("Receiving stopped: %1").arg(QString::fromUtf8(error.what())));
        return;
    }
    if (!addsResumed_) {
        addsResumed_ = true;
        resumePendingAdds();
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
        emit messageReceived(m);
    }
    emitContacts();
    emitDeviceState();
    emitFacadeInfo();
    refreshCalls();
    takenBytes_ += chunkBytes;
    if (catchingUp_) {
        catchingUp_ = !ended;
        emit syncProgress(catchingUp_, syncShown_, static_cast<qint64>(takenBytes_),
            static_cast<qint64>(streamBytes_));
    }
    if (more) {
        QMetaObject::invokeMethod(this, "sync", Qt::QueuedConnection);
    }
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

void SessionWorker::takeCourierNews()
{
    if (!session_) {
        return;
    }
    try {
        session_->flushPendingEchoes();
    } catch (const std::exception& error) {
        bazarish::log::warn("self-sync of our own sends failed: {}", error.what());
    }
    emitContacts();
}

void SessionWorker::maintain()
{
    if (!session_) {
        return;
    }
    const bazarish::log::Slow timed("the maintenance pass", kSlowPass);
    try {
        const bazarish::log::Slow timedEchoes("telling our own devices", kSlowStretch);
        session_->flushPendingEchoes();
    } catch (const std::exception& error) {
        bazarish::log::warn("self-sync of our own sends failed, next try on the next event: {}",
            error.what());
        sessionDutiesFailed_ = true;
    }
    session_->expireFileRequests(nowMillis());
    const std::optional<qint64> transientAt = transientCheckAtMs(nowMillis());
    if (transientAt.has_value() && nowMillis() >= *transientAt) {
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
    const std::optional<qint64> aliasAt = aliasAttemptAtMs();
    if (aliasAt.has_value() && nowMillis() >= *aliasAt) {
        startAliasErrand(/*byHand=*/false);
    }
    armSchedule();
}

void SessionWorker::emitCallState()
{
    if (!session_) {
        return;
    }
    const Session::CallInfo call = session_->currentCall();
    ToldCall told;
    told.state = static_cast<int>(call.state);
    told.peer = QString::fromStdString(call.peerFingerprint);
    told.callId = QString::fromStdString(call.callId);
    told.muted = call.muted;
    told.stage = noteForCallStage(call.stage);
    told.peerRinging = call.peerRinging;
    told.connectedAtMs = static_cast<qint64>(call.connectedAtMs);
    if (toldCall_ != told) {
        toldCall_ = told;
        emit callStateChanged(told.state, told.peer, told.callId, told.muted, told.stage,
            told.peerRinging, told.connectedAtMs);
    }
    const std::pair<float, float> levels{call.inputLevel, call.outputLevel};
    if (levels != toldLevels_) {
        toldLevels_ = levels;
        emit callLevels(levels.first, levels.second);
    }
    reconcileCallTimer();
}

void SessionWorker::reconcileCallTimer()
{
    const bool live = session_ && session_->currentCall().state != Session::CallState::eIdle;
    if (live) {
        if (callTimer_ == nullptr) {
            callTimer_ = new QTimer(this);
            callTimer_->setInterval(kCallWatchIntervalMs);
            connect(callTimer_, &QTimer::timeout, this, &SessionWorker::refreshCalls);
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
        if (call.record) {
            emit messageReceived(*call.record);
        }
        emit callLogged(
            QString::fromStdString(call.peer), call.incoming, static_cast<int>(call.outcome));
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
        emit callEndFailed(true, tr("No account is open"));
        return;
    }
    bazarish::client::DeliveryWatch watch;
    watch.onOutcome = [worker = QPointer<SessionWorker>(this)](
                          const bazarish::client::OutboundCourier::Outcome& outcome) {
        if (outcome.stored || worker.isNull()) {
            return;
        }
        emit worker->callEndFailed(true, refusalText(outcome));
    };
    try {
        session_->declineCall(callId.toStdString(), watch);
    } catch (const std::exception& error) {
        emit callEndFailed(true, QString::fromUtf8(error.what()));
    }
    emitCallState();
    flushCallLog();
}

void SessionWorker::endCall()
{
    if (!session_) {
        emit callEndFailed(false, tr("No account is open"));
        return;
    }
    try {
        session_->endCall();
    } catch (const std::exception& error) {
        emit callEndFailed(false, QString::fromUtf8(error.what()));
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

void SessionWorker::watchUnsent(const QVariantList& rows)
{
    if (!session_) {
        return;
    }
    for (const QVariant& held : rows) {
        const QVariantMap row = held.toMap();
        const qint64 localId = row.value(QStringLiteral("id")).toLongLong();
        if (!session_->watchDelivery(row.value(QStringLiteral("peer")).toString().toStdString(),
                row.value(QStringLiteral("e2eId")).toString().toStdString(),
                watchFor(*this, localId))) {
            emit sendResult(localId, false, tr("This message did not leave this device"));
        }
    }
}

void SessionWorker::sendText(const QString& peer, const QString& text, qint64 localId,
    const QString& e2eId, const QString& replyTo, const bool forwarded)
{
    try {
        openSession().sendMessage(peer.toStdString(), text.toStdString(), e2eId.toStdString(),
            watchFor(*this, localId), replyTo.toStdString(), forwarded);
        emitContacts();
    } catch (const std::exception& e) {
        emit sendResult(localId, false, QString::fromUtf8(e.what()));
    }
}

void SessionWorker::sendFile(const QString& peer, const QString& localPath, qint64 localId,
    const QString& e2eId, const QString& replyTo, const bool forwarded)
{
    try {
        openSession().sendFile(peer.toStdString(), localPath.toStdString(), e2eId.toStdString(),
            watchFor(*this, localId), replyTo.toStdString(), forwarded);
        emitContacts();
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
        openSession().sendVoice(peer.toStdString(), audio, durationMs, e2eId.toStdString(),
            watchFor(*this, localId), replyTo.toStdString(), forwarded);
        emitContacts();
    } catch (const std::exception& e) {
        emit sendResult(localId, false, QString::fromUtf8(e.what()));
    }
}

void SessionWorker::sendFileBytes(const QString& peer, const QByteArray& bytes,
    const QString& name, qint64 localId, const QString& e2eId, const QString& replyTo,
    const bool forwarded)
{
    try {
        openSession().sendFileBytes(peer.toStdString(), name.toStdString(),
            bazarish::Bytes(bytes.begin(), bytes.end()), e2eId.toStdString(),
            watchFor(*this, localId), replyTo.toStdString(), forwarded);
        emitContacts();
    } catch (const std::exception& e) {
        emit sendResult(localId, false, QString::fromUtf8(e.what()));
    }
}

void SessionWorker::sendPicture(const QString& peer, const QByteArray& bytes,
    const QString& name, const QString& mime, qint64 localId, const QString& e2eId,
    const QString& replyTo, const bool forwarded)
{
    try {
        openSession().sendPicture(peer.toStdString(),
            bazarish::Bytes(bytes.begin(), bytes.end()), name.toStdString(), mime.toStdString(),
            e2eId.toStdString(), watchFor(*this, localId), replyTo.toStdString(), forwarded);
        emitContacts();
    } catch (const std::exception& e) {
        emit sendResult(localId, false, QString::fromUtf8(e.what()));
    }
}

void SessionWorker::sendReceipt(const QString& peer, const QString& refId)
{
    const QString op = beginOp(tr("Read receipt"), tr("Sending..."));
    try {
        openSession().sendReceipt(peer.toStdString(), refId.toStdString());
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

void SessionWorker::askGroupMembers(const QString& peer)
{
    try {
        openSession().askGroupMembers(peer.toStdString());
    } catch (const std::exception& error) {
        bazarish::log::warn("member list not asked for: {}", error.what());
    }
}

void SessionWorker::sendReaction(const QString& peer, const QString& refId, const QString& emoji)
{
    const QString op = beginOp(
        emoji.isEmpty() ? tr("Removing reaction") : tr("Reaction %1").arg(emoji), tr("Sending..."));
    try {
        openSession().sendReaction(peer.toStdString(), refId.toStdString(), emoji.toStdString());
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
        openSession().sendCallback(
            peer.toStdString(), data.toStdString(), ref.toStdString(), botActionWatch(opId));
        emitContacts();
    } catch (const std::exception& e) {
        emit botActionDone(opId, false, QString::fromUtf8(e.what()));
    }
}

void SessionWorker::sendCommand(
    const QString& opId, const QString& peer, const QString& command, const QString& args)
{
    try {
        openSession().sendCommand(peer.toStdString(), command.toStdString(), args.toStdString(),
            {}, botActionWatch(opId));
        emitContacts();
    } catch (const std::exception& e) {
        emit botActionDone(opId, false, QString::fromUtf8(e.what()));
    }
}

void SessionWorker::sendEdit(
    const QString& peer, const QString& refId, qint64 localId, const QString& text)
{
    try {
        openSession().sendEdit(peer.toStdString(), refId.toStdString(), text.toStdString(), {},
            watchFor(*this, localId));
    } catch (const std::exception& e) {
        emit sendResult(localId, false, QString::fromUtf8(e.what()));
    }
}

void SessionWorker::dropSentFile(const QString& refId)
{
    try {
        openSession().unsend(refId.toStdString());
    } catch (const std::exception& error) {
        bazarish::log::debug("nothing to forget for a deleted message: {}", error.what());
    }
}

void SessionWorker::sendDelete(const QString& peer, const QString& refId)
{
    try {
        openSession().sendDelete(peer.toStdString(), refId.toStdString());
    } catch (const std::exception& e) {
        emit actionFailed(QString::fromUtf8(e.what()));
    }
}

QString SessionWorker::milestoneText(const bazarish::client::ConnectMilestone milestone)
{
    using bazarish::client::ConnectMilestone;
    switch (milestone) {
    case ConnectMilestone::eRouterStarting: return tr("Starting the I2P router");
    case ConnectMilestone::eBuiltInReseeds: return tr("Starting with the built-in reseeds");
    case ConnectMilestone::eTunnelsBuilding: return tr("Building your I2P tunnels");
    case ConnectMilestone::eTunnelsStillBuilding: return tr("I2P tunnels are still building");
    case ConnectMilestone::eServerLookup: return tr("Looking up the server's I2P address");
    case ConnectMilestone::eServerReached: return tr("Connected to the server over I2P");
    case ConnectMilestone::eRegistering: return tr("Registering with this server");
    case ConnectMilestone::eRegisteringDevice: return tr("Registered; registering this device");
    case ConnectMilestone::eCheckingAddress: return tr("Checking the address your server serves");
    case ConnectMilestone::ePublishingDestination: return tr("Publishing your own destination");
    case ConnectMilestone::eDelegating: return tr("Delegating your destination to the server");
    case ConnectMilestone::ePublishingCard: return tr("Publishing your contact card");
    case ConnectMilestone::eSyncingDevices: return tr("Syncing your address to your other devices");
    }
    throw std::logic_error("no words for this connection milestone");
}

QString SessionWorker::beginOp(const QString& title, const QString& status)
{
    const QString opId = QStringLiteral("op:") + QString::number(++opSeq_);
    emit opBegin(opId, QStringLiteral("service"), title, status);
    return opId;
}

void SessionWorker::addByInvite(const QString& uri, const QString& intro, const QString& opId,
    const QString& requestId)
{
    startContactAdd(/*byAlias=*/false, uri, intro, opId, requestId);
}

void SessionWorker::addByAlias(const QString& alias, const QString& intro, const QString& opId,
    const QString& requestId)
{
    startContactAdd(/*byAlias=*/true, alias, intro, opId, requestId);
}

void SessionWorker::startContactAdd(const bool byAlias, const QString& uriOrAlias,
    const QString& intro, const QString& opId, const QString& requestId)
{
    if (!session_) {
        emit contactAddDone(opId, false, encodeSystemNote(QT_TR_NOOP("no account open")));
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
        emit contactAddDone(opId, false, QString::fromUtf8(e.what()));
        emit actionFailed(QString::fromUtf8(e.what()));
        return;
    }
    using Step = bazarish::client::Session::CardStep;
    emit contactAddStage(opId,
        noteForCardStep(byAlias ? Step::eLookingUpAlias : Step::eFetchingCard, uriOrAlias, {}));

    if (!resolvedAdds_) {
        resolvedAdds_ = std::make_shared<ResolvedContactAddQueue>();
    }
    std::shared_ptr<ResolvedContactAddQueue> queue = resolvedAdds_;
    try {
        inBackground([context = std::move(context), request = std::move(request),
                        queue = std::move(queue), opId, alias = uriOrAlias,
                        self = QPointer<SessionWorker>(this)]() {
            Step step = request.byAlias ? Step::eLookingUpAlias : Step::eFetchingCard;
            const auto wake = [&self]() {
                QMetaObject::invokeMethod(self, "drainResolvedAdds", Qt::QueuedConnection);
            };
            const auto tell = [&queue, &opId, &wake](const QString& note) {
                {
                    const std::lock_guard<std::mutex> lock(queue->mutex);
                    queue->stages.push_back({opId, note});
                }
                wake();
            };
            bazarish::client::tellFetchStages(
                [&step, &tell, &alias](const bazarish::client::FetchStage stage) {
                    tell(noteForCardStep(step, alias, noteForFetchStage(stage)));
                });
            bazarish::client::Session::ContactCardResolved resolved
                = bazarish::client::Session::resolveContactCard(context, request,
                    [&step, &tell, &alias](const Step next) {
                        step = next;
                        tell(noteForCardStep(step, alias, {}));
                    });
            bazarish::client::tellFetchStages(nullptr);
            {
                const std::lock_guard<std::mutex> lock(queue->mutex);
                queue->results.push_back({opId, std::move(resolved)});
            }
            wake();
        });
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
        const QString what = QString::fromStdString(pending.request.uriOrAlias);
        const QString intro = QString::fromStdString(pending.request.introText);
        const QString requestId = QString::fromStdString(pending.request.requestId);
        emit contactAddResumed(opId, what, intro, requestId);
        startContactAdd(pending.request.byAlias, what, intro, opId, requestId);
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
        stages.swap(resolvedAdds_->stages);
        // A card found while offline is committed by the pass that follows the connect.
        if (session_->isConnected()) {
            ready.swap(resolvedAdds_->results);
        }
    }
    for (const auto& [opId, stage] : stages) {
        emit contactAddStage(opId, stage);
    }
    for (const ResolvedContactAddQueue::Entry& entry : ready) {
        const bazarish::client::Session::ContactCardResolved& resolved = entry.resolved;
        if (!resolved.ok) {
            const QString error = resolved.aliasUnknown
                ? tr("No such alias")
                : causeText(resolved.cause, cardStepSide(resolved.failedAt));
            if (resolved.aliasUnknown) {
                emit contactAliasUnknown(entry.opId, error);
            } else {
                emit contactAddDone(entry.opId, false, error);
            }
            emit actionFailed(error);
            continue;
        }
        const QString fingerprint = QString::fromStdString(resolved.fingerprint);
        const bool resending = session_->contactAwaitsAnswer(resolved.fingerprint);
        if (!resending && session_->hasContact(resolved.fingerprint)) {
            emit contactAlreadyKnown(entry.opId, fingerprint);
            continue;
        }
        const bazarish::client::DeliveryWatch watch = contactRequestWatch(entry.opId,
            fingerprint, QString::fromStdString(resolved.introText),
            QString::fromStdString(resolved.requestId), !resending);
        try {
            emit contactAddStage(
                entry.opId, encodeSystemNote(QT_TR_NOOP("Sending the contact request...")));
            if (resending) {
                session_->sendContactRequest(
                    resolved.fingerprint, resolved.requestId, resolved.introText, watch);
            } else {
                session_->commitContactAdd(resolved, watch);
                emitContacts();
            }
            emit contactAddResolved(entry.opId, fingerprint);
        } catch (const std::exception& e) {
            if (!resending) {
                session_->forgetUnansweredContact(resolved.fingerprint);
                emitContacts();
            }
            emit contactAddDone(entry.opId, false, QString::fromUtf8(e.what()));
            emit actionFailed(QString::fromUtf8(e.what()));
        }
    }
}

bazarish::client::DeliveryWatch SessionWorker::botActionWatch(const QString& opId)
{
    bazarish::client::DeliveryWatch watch;
    watch.onOutcome = [this, opId](const bazarish::client::OutboundCourier::Outcome& outcome) {
        QMetaObject::invokeMethod(
            this,
            [this, opId, outcome]() {
                emit botActionDone(
                    opId, outcome.stored, outcome.stored ? QString() : refusalText(outcome));
            },
            Qt::QueuedConnection);
    };
    return watch;
}

bazarish::client::DeliveryWatch SessionWorker::contactRequestWatch(const QString& opId,
    const QString& fingerprint, const QString& intro, const QString& requestId,
    const bool recordedHere)
{
    bazarish::client::DeliveryWatch watch;
    watch.onOutcome = [this, opId, fingerprint, intro, requestId, recordedHere,
                          sentAtMs = nowMillis()](
                          const bazarish::client::OutboundCourier::Outcome& outcome) {
        QMetaObject::invokeMethod(
            this,
            [this, opId, fingerprint, intro, requestId, recordedHere, sentAtMs, outcome]() {
                finishContactRequest(
                    opId, fingerprint, intro, requestId, recordedHere, sentAtMs, outcome);
            },
            Qt::QueuedConnection);
    };
    return watch;
}

void SessionWorker::finishContactRequest(const QString& opId, const QString& fingerprint,
    const QString& intro, const QString& requestId, const bool recordedHere,
    const qint64 sentAtMs,
    const bazarish::client::OutboundCourier::Outcome& outcome)
{
    if (outcome.stored) {
        emit actionOk(tr("Contact request sent"));
        emit contactRequestSent(fingerprint, intro, requestId, sentAtMs);
        emit contactAddDone(opId, true, encodeSystemNote(QT_TR_NOOP("Request sent")));
        return;
    }
    const std::optional<bazarish::ErrorCode> code
        = bazarish::errorCodeFromString(outcome.errorCode);
    if (session_ && recordedHere) {
        session_->settleContactRequest(fingerprint.toStdString(), outcome);
        emitContacts();
    }
    if (code == bazarish::ErrorCode::eContactRateLimited) {
        emit contactAddRateLimited(opId, fingerprint, requestId);
        return;
    }
    const QString reason = contactRefusalText(outcome);
    emit contactAddDone(opId, false, reason);
    emit actionFailed(reason);
}

void SessionWorker::noteCommandDone()
{
    emit commandFinished();
}

void SessionWorker::acceptContact(const QString& peer)
{
    WorkerOp op(*this, QStringLiteral("accept:") + peer, QStringLiteral("contact"),
        tr("Agreeing to a contact request"), tr("Telling your server..."));
    try {
        openSession().acceptContactRequest(peer.toStdString());
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

void SessionWorker::emitInvite()
{
    if (!session_) {
        return;
    }
    QString invite;
    QString problem;
    try {
        invite = QString::fromStdString(session_->inviteUri());
    } catch (const std::exception& error) {
        problem = QString::fromUtf8(error.what());
    }
    if (invite == lastInvite_ && problem == lastInviteProblem_) {
        return;
    }
    lastInvite_ = invite;
    lastInviteProblem_ = problem;
    emit inviteState(invite, problem);
}

void SessionWorker::setAvatar(const QByteArray& bytes)
{
    if (!session_) {
        return;
    }
    const QString op = beginOp(tr("Setting your avatar"), tr("Sending to your contacts..."));
    try {
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
    const QString op = beginOp(tr("Removing your avatar"), tr("Working..."));
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
        emitInvite();
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
    emit accountSettings(
        session_->acceptCalls(), session_->sendReceipts(), session_->showPresence());
}

void SessionWorker::syncChatClear(const QString& peer)
{
    if (!session_) {
        return;
    }
    try {
        session_->syncChatClearToSelf(peer.toStdString());
    } catch (const std::exception& error) {
        emit opDone(beginOp(tr("Chat clearing for your other devices"), tr("Sending...")), false,
            tr("Not sent: %1").arg(QString::fromUtf8(error.what())));
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
        emit opDone(beginOp(tr("Read mark for your other devices"), tr("Sending...")), false,
            tr("Not sent: %1").arg(QString::fromUtf8(error.what())));
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
        emit opDone(beginOp(tr("Chat pin for your other devices"), tr("Sending...")), false,
            tr("Not sent: %1").arg(QString::fromUtf8(error.what())));
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

void SessionWorker::sendChatTimer(const QString& peer, const qint64 seconds)
{
    withSession([&] {
        session_->sendChatTimer(peer.toStdString(), seconds);
    });
}

void SessionWorker::shutdown()
{
    stopEventWaiter();
    stopOverview();
    stopApprovalWaiter();
    stopErrands();
    stopMaintenance();
    if (callTimer_ != nullptr) {
        callTimer_->stop();
    }
    if (dayTimer_ != nullptr) {
        dayTimer_->stop();
    }
    if (session_) {
        session_->onCourierNews({});
        session_->onOwnDevicesChanged({});
        session_->unwatchConnectionLog();
    }
    {
        const std::lock_guard<std::mutex> lock(sessionLife_);
        session_.reset();
    }
    emit stopped();
}

void SessionWorker::leavePresenceNow()
{
    const std::lock_guard<std::mutex> lock(sessionLife_);
    if (session_) {
        session_->leavePresence();
    }
}

void SessionWorker::releaseLinksNow()
{
    const std::lock_guard<std::mutex> lock(sessionLife_);
    if (session_) {
        session_->releaseI2pLinks();
    }
}

void SessionWorker::askDevicesForContacts()
{
    withSession([&] {
        session_->askDevicesForContacts();
        emit actionOk(tr("Asked your other devices for your contacts"));
    });
}

void SessionWorker::watchConnectionLog(const bool on)
{
    if (!session_) {
        emit connectionLogReady({});
        return;
    }
    if (!on) {
        session_->unwatchConnectionLog();
        return;
    }
    const QPointer<SessionWorker> self(this);
    QVariantList lines;
    for (const bazarish::client::WireEvent& event :
        session_->watchConnectionLog([self](const bazarish::client::WireEvent& recorded) {
            QMetaObject::invokeMethod(
                self, [self, line = wireLine(recorded)]() { emit self->connectionLogLine(line); },
                Qt::QueuedConnection);
        })) {
        lines.append(wireLine(event));
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

void SessionWorker::runOp(const QString& id, const QString& kind, const QString& title,
    const QString& status, const std::function<QString()>& work, const FarSide side)
{
    WorkerOp op(*this, id, kind, title, status);
    try {
        op.succeed(work());
    } catch (const std::exception& error) {
        bazarish::log::warn("{} operation failed: {}", kind.toStdString(), error.what());
        const QString reason = failureText(error, side);
        op.fail(reason);
        emit actionFailed(reason);
    }
}

QString SessionWorker::loggedFailureText(const std::exception& error)
{
    bazarish::log::warn("account action failed: {}", error.what());
    return failureText(error, FarSide::eServer);
}

void SessionWorker::emitAliasHoldings()
{
    const QVariantList rows = aliasHoldingRows(session_->aliasNames());
    emit aliasHoldings(rows, aliasHoldingsNote(rows, session_->aliasDepositCovers()));
}

void SessionWorker::publishThisDeviceAddress()
{
    if (!session_) {
        return;
    }
    runOp(QStringLiteral("i2p-address"), QStringLiteral("status"), tr("Publishing your address"),
        tr("Telling your server..."), [this]() {
            session_->publishThisDeviceAddress();
            return tr("Your server serves this address now");
        });
    refreshI2pStatus();
}

void SessionWorker::publishFreshAddress()
{
    if (!session_) {
        return;
    }
    runOp(QStringLiteral("i2p-address"), QStringLiteral("status"), tr("Making a new address"),
        tr("Building it..."), [this]() {
            session_->publishFreshAddress();
            return tr("A new address is published");
        });
    refreshI2pStatus();
}

void SessionWorker::refreshContacts()
{
    if (session_) {
        emitContacts();
    }
}

void SessionWorker::emitI2pKeyState()
{
    const bool hasKey = session_->hasI2pDestination();
    emit i2pKeyState(hasKey, QString::fromStdString(
        hasKey ? session_->i2pAddress() : session_->servedDestination()));
}

void SessionWorker::refreshI2pStatus()
{
    if (!session_) {
        return;
    }
    emitInvite();
    emitI2pKeyState();
    const bool hasKey = session_->hasI2pDestination();
    WorkerOp op(*this, QStringLiteral("i2p-status"), QStringLiteral("status"),
        tr("Checking your destination"), tr("Asking your server..."));
    QString summary;
    try {
        const bazarish::client::I2pDestStatus s = session_->i2pDestStatus();
        if (s.approved() && s.transientExpires != 0) {
            summary = tr("Published — your destination is live");
        } else if (s.approval == "pending") {
            summary = s.registrationMessage.empty()
                ? tr("Awaiting operator approval — no destination until then")
                : QString::fromStdString(s.registrationMessage);
        } else if (hasKey) {
            summary = tr("Not published — nobody can reach you yet");
        } else {
            summary = tr("No destination key yet");
        }
    } catch (const std::exception& error) {
        bazarish::log::warn("destination status poll failed: {}", error.what());
        summary = hasKey ? tr("Destination key ready; connect to publish it")
                         : tr("No destination key yet");
    }
    op.succeed(summary);
    const Session::ApprovalState approval = session_->approvalState();
    emit approvalState(approval.pending, approval.refused, QString::fromStdString(approval.message));
}

void SessionWorker::watchOverview(const bool open)
{
    overviewOpen_ = open;
    if (!session_) {
        return;
    }
    if (!open) {
        if (overview_) {
            overview_->close();
        }
        return;
    }
    emitKeptOverview();
    if (!session_->isConnected() || session_->switchedOff()) {
        return;
    }
    if (!overview_) {
        bazarish::client::Session::ContactFetchContext context;
        try {
            context = session_->contactFetchContext();
        } catch (const std::exception& error) {
            bazarish::log::warn("no overview: {}", error.what());
            return;
        }
        const std::shared_ptr<bazarish::client::Client> asker
            = bazarish::client::Session::makeEventClient(context);
        overviewClient_ = asker;
        overview_ = std::make_unique<bazarish::client::OverviewWatch>(
            bazarish::client::OverviewWatch::Hooks{
                [asker](const std::string& since) { return asker->overview(since); },
                [this]() {
                    QMetaObject::invokeMethod(this, "releaseOverviewHold", Qt::QueuedConnection);
                },
                [this](const bazarish::client::AccountOverview& overview) {
                    QMetaObject::invokeMethod(
                        this,
                        [this, overview]() {
                            if (!session_) {
                                return;
                            }
                            session_->keepOverview(overview);
                            toldUsage_ = overview.usage;
                            emit overviewReady(overviewMap(overview), true);
                        },
                        Qt::QueuedConnection);
                },
                [this](const std::string&) {
                    QMetaObject::invokeMethod(
                        this, [this]() { emit overviewFailed(); }, Qt::QueuedConnection);
                },
            });
    }
    overview_->open();
}

void SessionWorker::stopOverview()
{
    if (overviewClient_) {
        overviewClient_->close();
    }
    overview_.reset();
    overviewClient_.reset();
}

void SessionWorker::releaseOverviewHold()
{
    if (!session_) {
        return;
    }
    Errand errand;
    errand.releaseOverview = true;
    queueErrand(std::move(errand));
}

void SessionWorker::emitKeptOverview()
{
    if (!session_) {
        return;
    }
    try {
        if (const std::optional<bazarish::client::AccountOverview> kept = session_->keptOverview()) {
            toldUsage_ = kept->usage;
            emit overviewReady(overviewMap(*kept), false);
        }
    } catch (const std::exception& error) {
        bazarish::log::warn("the kept overview is unreadable: {}", error.what());
    }
}

void SessionWorker::takeMailboxUsage(const bazarish::client::MailboxUsage& usage)
{
    if (!session_ || toldUsage_ == usage) {
        return;
    }
    toldUsage_ = usage;
    session_->keepMailboxUsage(usage);
    emit mailboxUsage(static_cast<qulonglong>(usage.usedBytes),
        static_cast<qulonglong>(usage.quotaBytes));
}

void SessionWorker::startApprovalWaiter()
{
    if (approvalWaiter_.joinable() || !session_) {
        return;
    }
    bazarish::client::Session::ContactFetchContext context;
    try {
        context = session_->contactFetchContext();
    } catch (const std::exception& error) {
        bazarish::log::warn("no approval waiter: {}", error.what());
        return;
    }
    approvalWaiterRunning_ = std::make_shared<std::atomic<bool>>(true);
    approvalGate_.restart();
    approvalWaiter_ = std::thread([this, context, running = approvalWaiterRunning_]() {
        std::unique_ptr<bazarish::client::Client> waiter;
        while (running->load()) {
            approvalGate_.asking();
            try {
                if (!waiter) {
                    waiter = bazarish::client::Session::makeEventClient(context);
                }
                const bazarish::client::I2pDestStatus status
                    = waiter->awaitApprovalChange(bazarish::client::kApprovalPending);
                if (!running->load()) {
                    return;
                }
                approvalGate_.answered();
                QMetaObject::invokeMethod(
                    this, [this, status]() { approvalAnswered(status); }, Qt::QueuedConnection);
                if (status.approval != bazarish::client::kApprovalPending) {
                    return;
                }
            } catch (const bazarish::client::ApiError& error) {
                if (!running->load()) {
                    return;
                }
                if (error.httpStatus >= kFirstClientError && error.httpStatus < kFirstServerError) {
                    const std::string reason = error.what();
                    QMetaObject::invokeMethod(
                        this, [this, reason]() { approvalRefused(reason); }, Qt::QueuedConnection);
                    return;
                }
                bazarish::log::info("approval wait failed: {}", error.what());
                const bazarish::client::MailWaitCause cause = waiter->serverBehindFacadeDown()
                    ? approvalGate_.awaitServer()
                    : approvalGate_.awaitCause([&waiter]() { return waiter->facadePublishedAnew(); });
                if (cause == bazarish::client::MailWaitCause::eStopped) {
                    return;
                }
                if (cause != bazarish::client::MailWaitCause::eStaleLeaseSet) {
                    waiter->liftStandOff();
                }
            } catch (const std::exception& error) {
                if (!running->load()) {
                    return;
                }
                bazarish::log::info("approval wait failed: {}", error.what());
                const bazarish::client::MailWaitCause cause = approvalGate_.awaitCause(
                    [&waiter]() { return waiter && waiter->facadePublishedAnew(); });
                if (cause == bazarish::client::MailWaitCause::eStopped) {
                    return;
                }
            }
        }
    });
}

void SessionWorker::stopApprovalWaiter()
{
    if (approvalWaiterRunning_) {
        approvalWaiterRunning_->store(false);
    }
    approvalGate_.stop();
    if (approvalWaiter_.joinable()) {
        // Only taking its link down ends a held wait before the server answers it.
        if (session_) {
            session_->releaseI2pLinks();
        }
        approvalWaiter_.join();
    }
    approvalWaiter_ = std::thread();
    approvalWaiterRunning_.reset();
}

void SessionWorker::approvalAnswered(const bazarish::client::I2pDestStatus& status)
{
    if (!session_) {
        return;
    }
    if (status.approval == bazarish::client::kApprovalPending) {
        session_->takeApproval(status);
    } else if (status.approved()) {
        session_->takeApproval(status);
        if (approvalWaiter_.joinable()) {
            approvalWaiter_.join();
        }
        approvalWaiter_ = std::thread();
        try {
            session_->registerAccount();
        } catch (const std::exception& error) {
            bazarish::log::warn("registering after approval failed: {}", error.what());
        }
        startEventWaiter();
        refreshI2pStatus();
        serviceAliasesIfDue();
    } else {
        approvalRefused({});
        return;
    }
    const Session::ApprovalState approval = session_->approvalState();
    emit approvalState(approval.pending, approval.refused, QString::fromStdString(approval.message));
}

void SessionWorker::approvalRefused(const std::string& reason)
{
    if (!session_) {
        return;
    }
    if (approvalWaiter_.joinable()) {
        approvalWaiter_.join();
    }
    approvalWaiter_ = std::thread();
    bazarish::log::info("the server declined this account");
    session_->refuseApproval(reason);
    emit approvalState(false, true, QString::fromStdString(reason));
}

void SessionWorker::revokeDevice(const QString& deviceId)
{
    if (!session_) {
        return;
    }
    runOp(QStringLiteral("device-revoke"), QStringLiteral("status"), tr("Revoking a device"),
        tr("Telling your server..."), [this, &deviceId]() {
            session_->revokeDevice(deviceId.toStdString());
            emit actionOk(tr("Device revoked"));
            return tr("Revoked");
        });
    emitDeviceState();
}

void SessionWorker::clearDeviceQueue(const QString& deviceId)
{
    if (!session_) {
        return;
    }
    runOp(QStringLiteral("device-clear"), QStringLiteral("status"), tr("Clearing a device's queue"),
        tr("Telling your server..."), [this, &deviceId]() {
            session_->clearDeviceQueue(deviceId.toStdString());
            emit actionOk(tr("The device's queue is cleared"));
            return tr("Cleared");
        });
    emitDeviceState();
}

void SessionWorker::reviewDevices(const QStringList& revoke)
{
    if (!session_) {
        return;
    }
    runOp(QStringLiteral("device-review"), QStringLiteral("status"), tr("Revoking a device"),
        tr("Telling your server..."), [this, &revoke]() {
            std::vector<std::string> ids;
            for (const QString& id : revoke) {
                ids.push_back(id.toStdString());
            }
            session_->reviewDevices(ids);
            return revoke.isEmpty() ? tr("Done") : tr("Revoked");
        });
    emitDeviceState();
}

void SessionWorker::leaveAccount()
{
    if (!session_) {
        emit accountLeft(false, tr("This account is not open"));
        return;
    }
    WorkerOp op(*this, QStringLiteral("account-leave"), QStringLiteral("status"),
        tr("Removing from this device"), tr("Telling your server..."));
    try {
        session_->leaveAccount();
        op.succeed(tr("Removed from the server"));
        emit accountLeft(true, {});
    } catch (const bazarish::client::ApiError& error) {
        constexpr int kFirstServerErrorStatus = 500;
        if (error.httpStatus > 0 && error.httpStatus < kFirstServerErrorStatus) {
            bazarish::log::info("the server holds nothing for this device ({}); only this "
                                "device's copy goes",
                error.what());
            op.succeed(tr("Already gone from the server"));
            emit accountLeft(true, {});
            return;
        }
        bazarish::log::warn("this device did not leave the account on the server: {}", error.what());
        const QString reason = failureText(error, FarSide::eServer);
        op.fail(reason);
        emit accountLeft(false, reason);
    } catch (const std::exception& error) {
        bazarish::log::warn("this device did not leave the account on the server: {}", error.what());
        const QString reason = failureText(error, FarSide::eServer);
        op.fail(reason);
        emit accountLeft(false, reason);
    }
}

void SessionWorker::closeAccountOnServer()
{
    if (!session_) {
        emit accountClosed(false, tr("This account is not open"));
        return;
    }
    WorkerOp op(*this, QStringLiteral("account-close"), QStringLiteral("status"),
        tr("Deleting the account"), tr("Telling your server..."));
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
        bazarish::log::warn("the account was not deleted on the server: {}", error.what());
        const QString reason = failureText(error, FarSide::eServer);
        op.fail(reason);
        emit accountClosed(false, reason);
    } catch (const std::exception& error) {
        bazarish::log::warn("the account was not deleted on the server: {}", error.what());
        const QString reason = failureText(error, FarSide::eServer);
        op.fail(reason);
        emit accountClosed(false, reason);
    }
}

void SessionWorker::generatePersonalKey()
{
    if (!session_) {
        return;
    }
    runOp(QStringLiteral("dest-key"), QStringLiteral("dest"), tr("Creating your destination key"),
        tr("Generating..."), [this]() {
            const QString address = QString::fromStdString(session_->ensureI2pDestination());
            emit i2pKeyState(true, address);
            emit actionOk(tr("Personal I2P key created"));
            return address;
        });
    refreshI2pStatus();
}

void SessionWorker::loadPersonalKey(const QString& path)
{
    if (!session_) {
        return;
    }
    try {
        session_->loadI2pDestination(readWhole(path));
        emit actionOk(tr("Personal I2P key loaded"));
    } catch (const std::exception& e) {
        emit actionFailed(QString::fromUtf8(e.what()));
    }
    refreshI2pStatus();
}

void SessionWorker::replacePersonalKey()
{
    if (!session_) {
        return;
    }
    runOp(QStringLiteral("new-dest-key"), QStringLiteral("dest"), tr("Raising a new address"),
        tr("Delegating it to your server..."), [this]() {
            session_->replaceOwnAddress();
            emit i2pKeyState(true, QString::fromStdString(session_->i2pAddress()));
            emit actionOk(tr("New address published; your contacts are being told"));
            return tr("Raised");
        });
    refreshI2pStatus();
    serviceAliasesIfDue();
}

void SessionWorker::setAliasBinding(const QString& alias, const bool on)
{
    if (!session_) {
        return;
    }
    runOp(QStringLiteral("alias-binding-") + alias, QStringLiteral("alias"),
        on ? tr("Pointing %1 here").arg(alias) : tr("Unpointing %1").arg(alias),
        tr("Asking the alias registry..."), [this, &alias, on]() {
            session_->setAliasBinding(alias.toStdString(), on);
            return on ? tr("Points here") : tr("Points nowhere");
        },
        FarSide::eRegistry);
    emitAliasHoldings();
}

void SessionWorker::setAliasWord(const QString& alias, const QString& word)
{
    if (!session_) {
        return;
    }
    runOp(QStringLiteral("alias-word-") + alias, QStringLiteral("alias"),
        word.isEmpty() ? tr("Taking the word off %1").arg(alias)
                       : tr("Setting the word of %1").arg(alias),
        tr("Asking the alias registry..."), [this, &alias, &word]() {
            session_->setAliasWord(alias.toStdString(), word.toStdString());
            return word.isEmpty() ? tr("No word") : tr("Word set");
        },
        FarSide::eRegistry);
    emitAliasHoldings();
}

void SessionWorker::retryRoutingTo(const QString& peer)
{
    if (!session_) {
        return;
    }
    session_->retryRoutingTo(peer.toStdString());
}

void SessionWorker::setDelegationDays(const int days)
{
    withSession([&] {
        session_->setDelegationDays(days);
    });
}

void SessionWorker::setShowPresence(const bool on)
{
    withSession([&] {
        session_->setShowPresence(on);
    });
}

void SessionWorker::setPresenceFocused(const bool focused)
{
    if (session_) {
        session_->setPresenceFocused(focused);
    }
}

void SessionWorker::setPresenceAway(const bool away)
{
    if (session_) {
        session_->setPresenceAway(away);
    }
}

void SessionWorker::setPresenceOrder(const QStringList& chatOrder)
{
    if (!session_) {
        return;
    }
    std::vector<std::string> order;
    for (const QString& peer : chatOrder) {
        order.push_back(peer.toStdString());
    }
    session_->setPresenceOrder(order);
}

void SessionWorker::watchPresence(const QStringList& peers)
{
    if (!session_) {
        return;
    }
    std::vector<std::string> watched;
    for (const QString& peer : peers) {
        watched.push_back(peer.toStdString());
    }
    session_->presenceWatch(std::move(watched));
}

void SessionWorker::presenceStoppedTyping(const QString& peer)
{
    if (session_) {
        session_->presenceStoppedTyping(peer.toStdString());
    }
}

void SessionWorker::presenceTyped(const QString& peer)
{
    if (session_) {
        session_->presenceTyped(peer.toStdString());
    }
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

void SessionWorker::publishPersonalDest(const bool renew)
{
    if (!session_) {
        emit destChangeSettled();
        return;
    }
    {
        WorkerOp op(*this, QStringLiteral("publish-dest"), QStringLiteral("dest"),
            renew ? tr("Renewing your destination") : tr("Publishing your destination"),
            tr("Delegating it to your server..."));
        try {
            session_->publishRouting();
            op.succeed(renew ? tr("Renewed") : tr("Published"));
            emit actionOk(renew ? tr("Destination renewed")
                                : tr("Routing published: your card now carries this destination"));
        } catch (const std::exception& e) {
            bazarish::log::warn("routing not published: {}", e.what());
            const QString reason = failureText(e, FarSide::eServer);
            op.fail(reason);
            emit actionFailed(reason);
        }
    }
    refreshI2pStatus();
    serviceAliasesIfDue();
    emit destChangeSettled();
}

void SessionWorker::disablePersonalDest()
{
    if (!session_) {
        emit destChangeSettled();
        return;
    }
    try {
        session_->disableI2pDest();
        emit actionOk(tr("I2P destination revoked"));
    } catch (const std::exception& e) {
        bazarish::log::warn("the I2P destination was not revoked: {}", e.what());
        emit actionFailed(failureText(e, FarSide::eServer));
    }
    refreshI2pStatus();
    emit destChangeSettled();
}

void SessionWorker::saveAttachment(const QString& peer, const QString& e2eId,
    const QString& announcedSha256, const QString& destPath, qint64 token)
{
    if (session_ == nullptr) {
        emit downloadFinished(token, false, tr("No open session"));
        return;
    }
    try {
        session_->requestFile(peer.toStdString(), e2eId.toStdString(),
            announcedSha256.toStdString(),
            fileTargetFor(destPath, QString::number(token) + QStringLiteral(".part")));
    } catch (const std::exception& e) {
        emit downloadFinished(token, false, QString::fromUtf8(e.what()));
    }
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
        bazarish::log::warn("pairing failed: {}", event.error);
        emit pairFinished(false, causeText(event.cause, FarSide::eDevice));
        break;
    }
}

void SessionWorker::startPairing(const bool primary)
{
    if (!session_) {
        return;
    }
    try {
        const bazarish::client::Session::PairingOffer offer = session_->startPairing(
            [this](const bazarish::client::Session::PairingEvent& event) {
                reportPairing(event);
            },
            roleOf(primary));
        emit pairOfferReady(
            QString::fromStdString(offer.uri), QString::fromStdString(offer.code));
    } catch (const std::exception& e) {
        bazarish::log::warn("pairing did not start: {}", e.what());
        emit pairFinished(false, failureText(e, FarSide::eDevice));
    }
}

void SessionWorker::stopPairing()
{
    if (!session_) {
        return;
    }
    session_->stopPairing();
}

void SessionWorker::exportAccount(
    const QString& path, const QString& password, const bool primary)
{
    WorkerOp op(*this, QStringLiteral("export"), QStringLiteral("account"),
        tr("Exporting your backup"), tr("Sealing the account..."));
    try {
        const bazarish::Bytes sealed
            = openSession().exportAccountBytes(password.toStdString(), roleOf(primary));
        const std::unique_ptr<bazarish::client::FileOutput> out = openOutput(path);
        out->write(sealed);
        out->finish();
        op.succeed(tr("Backup exported"));
        emit actionOk(tr("Backup exported"));
    } catch (const std::exception& e) {
        op.fail(QString::fromUtf8(e.what()));
        emit actionFailed(QString::fromUtf8(e.what()));
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
            emit aliasActivationDone(false, tr("No account is open"));
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
    lastAliasServiceMs_ = nowMillis();
    try {
        inBackground([context = std::move(context), queue = std::move(queue),
                         self = QPointer<SessionWorker>(this)]() {
            bazarish::client::Session::AliasErrandResult result
                = bazarish::client::Session::runAliasErrand(context);
            {
                const std::lock_guard<std::mutex> lock(queue->mutex);
                queue->results.push_back(std::move(result));
            }
            QMetaObject::invokeMethod(self, "drainAliasErrands", Qt::QueuedConnection);
        });
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
                bazarish::log::info("name servicing failed: {}", result.error);
                emit aliasActivationDone(false, causeText(result.cause, FarSide::eRegistry));
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

bazarish::client::Session& SessionWorker::openSession()
{
    if (!session_) {
        throw std::runtime_error(tr("No account is open").toStdString());
    }
    return *session_;
}

}  // namespace bazarish::app
