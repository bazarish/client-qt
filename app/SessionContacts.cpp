// Bazarish project (c) 2026
#include "SessionController.hpp"

#include "SessionShared.hpp"
#include "SystemNotes.hpp"

#include "DeliveryStatus.hpp"

#include <bazarish/Address.hpp>
#include <bazarish/Crypto.hpp>
#include <bazarish/Limits.hpp>
#include <bazarish/Descriptor.hpp>
#include <bazarish/Portal.hpp>

#pragma push_macro("emit")
#undef emit
#include <bazarish/Log.hpp>
#pragma pop_macro("emit")

#include <QRegularExpression>
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

namespace {
constexpr int kContactRetryAttempts = 3;
constexpr int kContactRetrySeconds = 20;
constexpr int kMillisecondsPerSecond = 1000;
}  // namespace

void SessionController::activateAliasServicing()
{
    if (aliasBusy_) {
        return;
    }
    aliasBusy_ = true;
    emit aliasChanged();
    beginOperation(kAliasOperationId, QStringLiteral("alias"),
        tr("Checking your aliases"), tr("Asking the registry over I2P…"));
    emit requestActivateAliasServicing();
}

QString SessionController::inviteProblem(const QString& uri) const
{
    const QString trimmed = uri.trimmed();
    if (trimmed.isEmpty()) {
        return tr("Paste an invite link.");
    }
    try {
        const bazarish::Descriptor descriptor = bazarish::parseDescriptor(trimmed.toStdString());
        if (descriptor.dest.empty()) {
            return tr("This invite carries no address to reach that account.");
        }
    } catch (const std::exception&) {
        static const QRegularExpression fingerprint(QStringLiteral("^[a-z2-7]{52}$"));
        if (fingerprint.match(trimmed).hasMatch()) {
            return tr("That is a fingerprint, not an invite. An invite starts with "
                      "bazarish://invite?");
        }
        return tr("Not a bazarish:// invite. An alias starts with !.");
    }
    return {};
}

QString SessionController::aliasProblem(const QString& typed) const
{
    try {
        bazarish::normalizeAlias(typed.trimmed().toStdString());
    } catch (const std::exception& error) {
        return QString::fromUtf8(error.what());
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
        return;
    }
    try {
        const bazarish::Descriptor known
            = bazarish::parseDescriptor(uri.trimmed().toStdString());
        const QString peer = QString::fromStdString(known.fingerprint);
        if (contacts_.has(peer)) {
            openConversation(peer);
            emit actionOk(tr("Already in your contacts"));
            return;
        }
    } catch (const std::exception&) {
        // error-hiding: allowed - the link was vetted; the add below parses it and reports again.
    }
    const QString opId = QStringLiteral("contact:") + newE2eId();
    beginOperation(opId, QStringLiteral("contact"), tr("Adding contact"),
        tr("Preparing…"));
    try {
        const bazarish::Descriptor descriptor
            = bazarish::parseDescriptor(uri.trimmed().toStdString());
        const QString peer = QString::fromStdString(descriptor.fingerprint);
        if (!descriptor.name.empty()) {
            pendingContactNames_.insert(peer,
                QString::fromStdString(bazarish::client::safeContactName(descriptor.name)));
        }
        openContactProgress(peer, opId);
        refusedRequests_.insert(peer,
            PendingContactRequest{uri, intro, kContactRetryAttempts, requestId});
    } catch (const std::exception& error) {
        bazarish::log::warn("invite parsed for the chat but not for its peer: {}", error.what());
    }
    emit requestAddByInvite(uri, intro, opId, requestId);
}

void SessionController::onContactAddRateLimited(
    const QString& opId, const QString& fingerprint, const QString& requestId)
{
    finishOperation(opId, false, tr("Their address is busy"));
    contactProgressRows_.remove(opId);
    const auto found = refusedRequests_.find(fingerprint);
    if (found != refusedRequests_.end() && found->requestId.isEmpty()) {
        found->requestId = requestId;
    }
    if (found == refusedRequests_.end() || found->triesLeft <= 0) {
        writeConversationNote(fingerprint,
            encodeSystemNote(QT_TR_NOOP("The request was refused: the contact's server is busy.")));
        emit contactRetryExhausted(fingerprint);
        return;
    }
    --found->triesLeft;
    writeConversationNote(fingerprint,
        encodeSystemNote(QT_TR_NOOP("Their server is busy. Trying again in %1 seconds (%2 left)."),
            {QString::number(kContactRetrySeconds), QString::number(found->triesLeft + 1)}));
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
            continue;
        }
        agreeingShown_.insert(fingerprint);
        beginOperation(QStringLiteral("agreeing:") + fingerprint, QStringLiteral("contact"),
            tr("Agreeing to a contact request"),
            tr("Waiting for their server…"), fingerprint);
    }
    for (auto at = agreeingShown_.begin(); at != agreeingShown_.end();) {
        if (contactState_.value(*at).request == ContactState::eAccepting) {
            ++at;
            continue;
        }
        finishOperation(QStringLiteral("agreeing:") + *at, true, tr("Agreed"));
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
        emit actionFailed(tr("This add cannot be tried again from here"));
        return;
    }
    addByInvite(found->uri, found->intro, found->requestId);
}

void SessionController::addByAlias(const QString& alias, const QString& intro)
{
    const QString opId = QStringLiteral("contact:") + newE2eId();
    beginOperation(opId, QStringLiteral("contact"), tr("Adding %1").arg(alias),
        tr("Preparing…"));
    emit requestAddByAlias(alias, intro, opId);
}

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
    contacts_.touch(peer, peerName(peer), systemNoteText(text), note.ts, false);
    showInActiveView(note, true);
}

void SessionController::onRoutingTold(const QString& peer, const bool delivered)
{
    if (peer.isEmpty()) {
        return;
    }
    const QString kind = QStringLiteral("routing.failed");
    const qint64 told = store_.oldestOfType(peer, kind, /*outgoing=*/false);
    if (delivered) {
        if (told > 0) {
            store_.removeById(told);
            conversation_.removeById(told);
        }
        return;
    }
    if (told > 0) {
        return;
    }
    StoredMessage note;
    note.peer = peer;
    note.type = kind;
    note.text = encodeSystemNote(
        QT_TR_NOOP("Your new contact details did not reach them."));
    note.ts = nowMillis();
    note.orderKey = note.ts;
    note.status = DeliveryStatus::Received;
    note.id = store_.append(note);
    contacts_.touch(peer, peerName(peer), systemNoteText(note.text), note.ts, false);
    showInActiveView(note, true);
}

void SessionController::openContactProgress(const QString& peer, const QString& opId)
{
    if (peer.isEmpty()) {
        return;
    }
    StoredMessage note;
    note.peer = peer;
    note.type = QStringLiteral("system");
    note.text = encodeSystemNote(QT_TR_NOOP("Sending a contact request…"));
    note.ts = nowMillis();
    note.orderKey = note.ts;
    note.status = DeliveryStatus::Preparing;
    note.id = store_.append(note);
    contactProgressRows_[opId] = ContactProgressRow{note.id, peer};
    contacts_.touch(peer, peerName(peer), systemNoteText(note.text), note.ts, false);
    if (activePeer_ == peer) {
        showInActiveView(note, true);
        return;
    }
    openConversation(peer);
}

void SessionController::writeContactProgress(const QString& opId, const QString& text)
{
    const auto found = contactProgressRows_.constFind(opId);
    if (found == contactProgressRows_.cend()) {
        return;
    }
    store_.editContent(found->id, text, QString());
    conversation_.setTextForId(found->id, text);
    contacts_.touch(found->peer, peerName(found->peer), chatPreview(found->peer), 0, false);
}

void SessionController::acceptContact()
{
    if (activePeer_.isEmpty()) {
        return;
    }
    const QString peer = activePeer_;
    if (acceptingContact_ == peer) {
        return;
    }
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
    return acceptingContact_ == fp
        || contactState_.value(fp).request == ContactState::eAccepting;
}

void SessionController::requestInvite()
{
    emit requestInviteSig();
}

void SessionController::onContactAddStage(const QString& opId, const QString& status)
{
    updateOperation(opId, systemNoteText(status));
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
        return;
    }
    contactState_[peer].request = ContactState::eAnswered;
    ++contactsRevision_;
    emit contactsRevisionChanged();
}

void SessionController::onContactAddDone(const QString& opId, bool ok, const QString& status)
{
    emit requestForgetPendingAdd(opId);
    finishOperation(opId, ok, systemNoteText(status));
    const auto row = contactProgressRows_.constFind(opId);
    if (row != contactProgressRows_.cend()) {
        store_.updateStatus(row->id, DeliveryStatus::Received);
        if (!ok) {
            store_.setType(row->id, QStringLiteral("contact.failed"));
            conversation_.setTypeForId(row->id, QStringLiteral("contact.failed"));
        }
    }
    writeContactProgress(opId,
        ok ? status : encodeSystemNote(QT_TR_NOOP("Could not add: %1"), {systemNoteText(status)}));
    contactProgressRows_.remove(opId);
}

void SessionController::onContactAlreadyKnown(const QString& opId, const QString& fingerprint)
{
    emit requestForgetPendingAdd(opId);
    finishOperation(opId, true, tr("Already in your contacts"));
    contactProgressRows_.remove(opId);
    openConversation(fingerprint);
    emit actionOk(tr("Already in your contacts"));
}

void SessionController::onContactRequestSent(
    const QString& fingerprint, const QString& intro, const QString& requestId)
{
    if (fingerprint.isEmpty()) {
        return;
    }
    if (store_.oldestOfType(fingerprint, QStringLiteral("contact.request"), /*outgoing=*/true)
        != 0) {
        if (activePeer_ != fingerprint) {
            openConversation(fingerprint);
        }
        return;
    }
    const QString body = intro.isEmpty() ? tr("Contact request sent.") : intro;
    StoredMessage m;
    m.peer = fingerprint;
    m.outgoing = true;
    m.type = "contact.request";
    m.e2eId = requestId.isEmpty() ? newE2eId() : requestId;
    m.text = body;
    m.ts = nowMillis();
    m.orderKey = m.ts;
    m.status = DeliveryStatus::AtRecipientServer;
    m.id = store_.append(m);
    statusById_[m.id] = m.status;
    contacts_.touch(fingerprint, {}, body, m.ts, false);
    if (activePeer_ == fingerprint) {
        showInActiveView(m, true);
        return;
    }
    openConversation(fingerprint);
}

}  // namespace bazarish::app
