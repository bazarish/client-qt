// Bazarish project (c) 2026
#include "SessionController.hpp"

#include "SessionShared.hpp"

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

namespace {
// A contact request the recipient's address refused for being over its cap is
// sent again on a timer: enough tries to ride out a busy minute, spaced so the
// next one lands in a fresh window.
constexpr int kContactRetryAttempts = 3;
constexpr int kContactRetrySeconds = 20;
constexpr int kMillisecondsPerSecond = 1000;
}  // namespace

// Adding a correspondent: the invite or alias it starts from, and the wait.

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
    const QString opId = QStringLiteral("contact:") + newE2eId();
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
    const QString opId = QStringLiteral("contact:") + newE2eId();
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

void SessionController::onContactAddStage(const QString& opId, const QString& status)
{
    updateOperation(opId, status);
    writeContactProgress(opId, status);
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
    m.e2eId = requestId.isEmpty() ? newE2eId() : requestId;
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

}  // namespace bazarish::app
