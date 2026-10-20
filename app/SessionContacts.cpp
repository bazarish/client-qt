// Bazarish project (c) 2026
#include "SessionController.hpp"

#include "SessionShared.hpp"
#include "SystemNotes.hpp"

#include "DeliveryStatus.hpp"
#include "Session.hpp"

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

void SessionController::activateAliasServicing()
{
    if (aliasBusy_) {
        return;
    }
    aliasBusy_ = true;
    emit aliasChanged();
    emit commands_.requestActivateAliasServicing();
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
        return tr("An alias must start with \"!\", and a full link with \"bazarish://\".");
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
    const QString problem = inviteProblem(uri);
    if (!problem.isEmpty()) {
        emit actionFailed(problem);
        return;
    }
    emit commands_.addByInvite(uri, intro);
}

void SessionController::retryContactRequest(const QString& fingerprint)
{
    emit commands_.retryContactRequest(fingerprint);
}

void SessionController::retryContactAdd()
{
    if (!activePeer_.isEmpty()) {
        emit commands_.retryContactAdd(activePeer_);
    }
}

void SessionController::addByAlias(const QString& alias, const QString& intro)
{
    const QString problem = aliasProblem(alias);
    if (!problem.isEmpty()) {
        emit actionFailed(problem);
        return;
    }
    emit commands_.addByAlias(alias, intro);
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
    emit commands_.requestAcceptContact(peer);
}

bool SessionController::contactCanAccept(const QString& fp) const
{
    return book_.state(fp).request != ContactState::eAnswered;
}

bool SessionController::contactAgreeing(const QString& fp) const
{
    return acceptingContact_ == fp
        || book_.state(fp).request == ContactState::eAccepting;
}

void SessionController::requestInvite()
{
    emit commands_.requestInviteSig();
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
    book_.edit(peer).request = ContactState::eAnswered;
    ++contactsRevision_;
    emit contactsRevisionChanged();
}

}  // namespace bazarish::app
