// Bazarish project (c) 2026
#include "CoreAccount.hpp"

#include "DeliveryStatus.hpp"
#include "Session.hpp"
#include "SessionShared.hpp"
#include "SystemNotes.hpp"

#include <bazarish/Address.hpp>
#include <bazarish/Descriptor.hpp>

#pragma push_macro("emit")
#undef emit
#include <bazarish/Log.hpp>
#pragma pop_macro("emit")

#include <QTimer>

namespace bazarish::app {

namespace {
constexpr int kContactRetryAttempts = 3;
constexpr int kContactRetrySeconds = 20;
constexpr int kMillisecondsPerSecond = 1000;
}  // namespace

void CoreAccount::addByInvite(const QString& uri, const QString& intro)
{
    try {
        const bazarish::Descriptor known
            = bazarish::parseDescriptor(uri.trimmed().toStdString());
        const QString peer = QString::fromStdString(known.fingerprint);
        if (contacts_.has(peer) && contacts_.state(peer).writable) {
            emit events_.showConversation(peer);
            emit events_.actionOk(tr("Already in your contacts"));
            return;
        }
    } catch (const std::exception& error) {
        emit events_.actionFailed(QString::fromUtf8(error.what()));
        return;
    }
    sendContactAdd(uri, intro, newE2eId());
}

void CoreAccount::addByAlias(const QString& alias, const QString& intro)
{
    startAliasAdd(alias, intro, newE2eId(), aliasChatPeer(alias));
}

void CoreAccount::retryContactAdd(const QString& peer)
{
    const auto found = refusedRequests_.constFind(peer);
    if (found == refusedRequests_.cend()) {
        emit events_.actionFailed(tr("This add cannot be tried again from here"));
        return;
    }
    const PendingContactRequest pending = *found;
    repeatContactAdd(peer, pending);
}

void CoreAccount::sendContactAdd(
    const QString& uri, const QString& intro, const QString& requestId)
{
    const QString opId = QStringLiteral("contact:") + newE2eId();
    log_.begin(opId, QStringLiteral("contact"), tr("Adding contact"),
        tr("Preparing…"));
    trackContactAdd(opId, uri, intro, requestId);
    emit commands_.requestAddByInvite(uri, intro, opId, requestId);
}

void CoreAccount::onContactAddResumed(
    const QString& opId, const QString& uri, const QString& intro, const QString& requestId)
{
    log_.begin(opId, QStringLiteral("contact"), tr("Adding a contact"),
        tr("Resuming after a restart…"));
    if (hasAliasSigil(uri)) {
        trackAliasAdd(opId, uri, intro, requestId, aliasChatPeer(uri));
        return;
    }
    trackContactAdd(opId, uri, intro, requestId);
}

void CoreAccount::trackContactAdd(
    const QString& opId, const QString& uri, const QString& intro, const QString& requestId)
{
    try {
        const bazarish::Descriptor descriptor
            = bazarish::parseDescriptor(uri.trimmed().toStdString());
        const QString peer = QString::fromStdString(descriptor.fingerprint);
        if (!descriptor.name.empty()) {
            const QString name
                = QString::fromStdString(bazarish::client::safeContactName(descriptor.name));
            contacts_.notePendingName(peer, name);
            emit events_.contactNamed(peer, name);
        }
        openContactProgress(peer, opId);
        refusedRequests_.insert(peer,
            PendingContactRequest{uri, intro, kContactRetryAttempts, requestId});
    } catch (const std::exception& error) {
        bazarish::log::warn("invite parsed for the chat but not for its peer: {}", error.what());
    }
}

void CoreAccount::onContactAddRateLimited(
    const QString& opId, const QString& fingerprint, const QString& requestId)
{
    emit commands_.requestForgetPendingAdd(opId);
    log_.finish(opId, false, tr("Their address is busy"));
    contactProgressRows_.remove(opId);
    const auto found = refusedRequests_.find(fingerprint);
    if (found != refusedRequests_.end() && found->requestId.isEmpty()) {
        found->requestId = requestId;
    }
    if (found == refusedRequests_.end() || found->triesLeft <= 0) {
        writeConversationNote(fingerprint,
            encodeSystemNote(QT_TR_NOOP("The request was refused: the contact's server is busy.")));
        emit events_.contactRetryExhausted(fingerprint);
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

void CoreAccount::retryContactRequest(const QString& fingerprint)
{
    const auto found = refusedRequests_.find(fingerprint);
    if (found == refusedRequests_.end()) {
        return;
    }
    const PendingContactRequest pending = *found;
    repeatContactAdd(fingerprint, pending);
    if (const auto again = refusedRequests_.find(fingerprint); again != refusedRequests_.end()) {
        again->triesLeft = pending.triesLeft;
        again->requestId = pending.requestId;
    }
}

void CoreAccount::syncAgreeingRows()
{
    for (const QString& fingerprint : contacts_.accepting()) {
        if (agreeingShown_.contains(fingerprint)
            || log_.has(QStringLiteral("accept:") + fingerprint)) {
            continue;
        }
        agreeingShown_.insert(fingerprint);
        log_.begin(QStringLiteral("agreeing:") + fingerprint, QStringLiteral("contact"),
            tr("Agreeing to a contact request"),
            tr("Waiting for their server…"), fingerprint);
    }
    for (auto at = agreeingShown_.begin(); at != agreeingShown_.end();) {
        if (contacts_.state(*at).request == ContactState::eAccepting) {
            ++at;
            continue;
        }
        log_.finish(QStringLiteral("agreeing:") + *at, true, tr("Agreed"));
        at = agreeingShown_.erase(at);
    }
}

void CoreAccount::repeatContactAdd(const QString& chat, const PendingContactRequest& pending)
{
    const QString failed = QStringLiteral("contact.failed");
    qint64 removed = 0;
    for (qint64 row = store_.oldestOfType(chat, failed, /*outgoing=*/false);
         row != 0 && row != removed; row = store_.oldestOfType(chat, failed, /*outgoing=*/false)) {
        if (!store_.removeById(row)) {
            reportUnsaved();
            break;
        }
        emit events_.rowRemoved(chat, row);
        removed = row;
    }
    if (hasAliasSigil(pending.uri)) {
        startAliasAdd(pending.uri, pending.intro, pending.requestId, chat);
        return;
    }
    sendContactAdd(pending.uri, pending.intro, pending.requestId);
}

void CoreAccount::startAliasAdd(
    const QString& alias, const QString& intro, const QString& requestId, const QString& chat)
{
    const QString opId = QStringLiteral("contact:") + newE2eId();
    log_.begin(opId, QStringLiteral("contact"), tr("Adding %1").arg(alias),
        tr("Preparing…"));
    trackAliasAdd(opId, alias, intro, requestId, chat);
    emit commands_.requestAddByAlias(alias, intro, opId, requestId);
}

void CoreAccount::trackAliasAdd(const QString& opId, const QString& alias,
    const QString& intro, const QString& requestId, const QString& chat)
{
    if (hasAliasSigil(chat)) {
        aliasChats_.insert(opId, chat);
    }
    openContactProgress(chat, opId);
    refusedRequests_.insert(chat,
        PendingContactRequest{alias, intro, kContactRetryAttempts, requestId});
}

void CoreAccount::writeConversationNote(const QString& peer, const QString& text)
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
    if (note.id == 0) {
        reportUnsaved();
        return;
    }
    emit events_.rowAdded(peer, note.id, true);
}

void CoreAccount::onRoutingTold(const QString& peer, const bool delivered)
{
    if (peer.isEmpty()) {
        return;
    }
    const QString kind = QStringLiteral("routing.failed");
    const qint64 told = store_.oldestOfType(peer, kind, /*outgoing=*/false);
    if (delivered) {
        if (told > 0) {
            if (!store_.removeById(told)) {
                reportUnsaved();
                return;
            }
            emit events_.rowRemoved(peer, told);
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
    if (note.id == 0) {
        reportUnsaved();
        return;
    }
    emit events_.rowAdded(peer, note.id, true);
}

void CoreAccount::openContactProgress(const QString& peer, const QString& opId)
{
    if (peer.isEmpty()) {
        return;
    }
    noteContactProgress(peer, opId);
    if (viewedPeer_ != peer) {
        emit events_.showConversation(peer);
    }
}

void CoreAccount::noteContactProgress(const QString& peer, const QString& opId)
{
    StoredMessage note;
    note.peer = peer;
    note.type = QStringLiteral("system");
    note.text = encodeSystemNote(QT_TR_NOOP("Sending a contact request…"));
    note.ts = nowMillis();
    note.orderKey = note.ts;
    note.status = DeliveryStatus::Preparing;
    note.id = store_.append(note);
    if (note.id == 0) {
        reportUnsaved();
        return;
    }
    contactProgressRows_[opId] = ContactProgressRow{note.id, peer};
    emit events_.rowAdded(peer, note.id, true);
}

void CoreAccount::writeContactProgress(const QString& opId, const QString& text)
{
    const auto found = contactProgressRows_.constFind(opId);
    if (found == contactProgressRows_.cend()) {
        return;
    }
    if (!store_.editContent(found->id, text, QString())) {
        reportUnsaved();
        return;
    }
    emit events_.rowChanged(found->peer, found->id);
}

void CoreAccount::onContactAddStage(const QString& opId, const QString& status)
{
    log_.update(opId, systemNoteText(status));
    writeContactProgress(opId, status);
}

void CoreAccount::onContactAddDone(const QString& opId, const bool ok, const QString& status)
{
    settleContactAdd(opId, ok,
        ok ? status : encodeSystemNote(QT_TR_NOOP("Could not add: %1"), {systemNoteText(status)}));
}

void CoreAccount::onContactRequestUnconfirmed(const QString& opId)
{
    settleContactAdd(opId, false,
        encodeSystemNote(QT_TR_NOOP("The contact's server did not confirm the request.")));
}

void CoreAccount::settleContactAdd(const QString& opId, const bool ok, const QString& note)
{
    aliasChats_.remove(opId);
    emit commands_.requestForgetPendingAdd(opId);
    log_.finish(opId, ok, systemNoteText(note));
    const auto row = contactProgressRows_.constFind(opId);
    if (row != contactProgressRows_.cend()) {
        if (!store_.updateStatus(row->id, DeliveryStatus::Received)
            || (!ok && !store_.setType(row->id, QStringLiteral("contact.failed")))) {
            reportUnsaved();
        }
    }
    writeContactProgress(opId, note);
    contactProgressRows_.remove(opId);
}

void CoreAccount::onContactAlreadyKnown(const QString& opId, const QString& fingerprint)
{
    emit commands_.requestForgetPendingAdd(opId);
    log_.finish(opId, true, tr("Already in your contacts"));
    contactProgressRows_.remove(opId);
    if (const QString chat = aliasChats_.take(opId); !chat.isEmpty()) {
        refusedRequests_.remove(chat);
        forgetChat(chat);
    }
    emit events_.showConversation(fingerprint);
    emit events_.actionOk(tr("Already in your contacts"));
}

void CoreAccount::onContactAliasUnknown(const QString& opId, const QString& status)
{
    const QString chat = aliasChats_.take(opId);
    if (chat.isEmpty()) {
        onContactAddDone(opId, false, status);
        return;
    }
    emit commands_.requestForgetPendingAdd(opId);
    log_.finish(opId, false, status);
    contactProgressRows_.remove(opId);
    refusedRequests_.remove(chat);
    forgetChat(chat);
}

void CoreAccount::onContactAddResolved(const QString& opId, const QString& fingerprint)
{
    const QString chat = aliasChats_.take(opId);
    if (chat.isEmpty()) {
        return;
    }
    const bool shown = viewedPeer_ == chat;
    if (const auto found = refusedRequests_.constFind(chat); found != refusedRequests_.cend()) {
        const PendingContactRequest pending = *found;
        refusedRequests_.remove(chat);
        refusedRequests_.insert(fingerprint, pending);
    }
    contactProgressRows_.remove(opId);
    forgetChat(chat);
    if (shown) {
        openContactProgress(fingerprint, opId);
    } else {
        noteContactProgress(fingerprint, opId);
    }
}

void CoreAccount::onContactRequestSent(const QString& fingerprint, const QString& intro,
    const QString& requestId, const qint64 sentAtMs)
{
    if (fingerprint.isEmpty()) {
        return;
    }
    if (store_.oldestOfType(fingerprint, QStringLiteral("contact.request"), /*outgoing=*/true)
        != 0) {
        if (viewedPeer_ != fingerprint) {
            emit events_.showConversation(fingerprint);
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
    m.ts = sentAtMs;
    m.orderKey = m.ts;
    const bool answered = contacts_.has(fingerprint)
        && contacts_.state(fingerprint).request == ContactState::eAnswered;
    m.status = answered ? DeliveryStatus::Delivered : DeliveryStatus::AtRecipientServer;
    m.id = store_.append(m);
    if (m.id == 0) {
        reportUnsaved();
        return;
    }
    emit events_.rowAdded(fingerprint, m.id, true);
    if (viewedPeer_ != fingerprint) {
        emit events_.showConversation(fingerprint);
    }
}

}  // namespace bazarish::app
