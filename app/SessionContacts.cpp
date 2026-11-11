// Bazarish project (c) 2026
#include "SessionController.hpp"

#include "SessionShared.hpp"

#include "Session.hpp"

#include <bazarish/Address.hpp>
#include <bazarish/Crypto.hpp>
#include <bazarish/Limits.hpp>
#include <bazarish/Links.hpp>
#include <bazarish/Descriptor.hpp>
#include <bazarish/Portal.hpp>

#pragma push_macro("emit")
#undef emit
#include <bazarish/Log.hpp>
#pragma pop_macro("emit")

#include <chrono>
#include <QTimer>
#include <cstring>

#include <algorithm>
#include <array>
#include <ctime>
#include <exception>
#include <fstream>
#include <iterator>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
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
    try {
        const bazarish::Descriptor descriptor
            = bazarish::parseDescriptor(uri.trimmed().toStdString());
        if (descriptor.dest.empty()) {
            return tr("This invite carries no address to reach that account");
        }
    } catch (const std::exception&) {
        return tr("This invite link is damaged");
    }
    return {};
}

QString SessionController::contactFor(const QString& uri) const
{
    if (!inviteProblem(uri).isEmpty()) {
        return {};
    }
    const QString fingerprint = QString::fromStdString(
        bazarish::parseDescriptor(uri.trimmed().toStdString()).fingerprint);
    return book_.has(fingerprint) ? fingerprint : QString();
}

QString SessionController::aliasProblem(const QString& typed) const
{
    const std::optional<bazarish::AliasProblem> problem
        = bazarish::aliasQueryProblem(typed.trimmed().toStdString());
    if (!problem.has_value()) {
        return {};
    }
    switch (*problem) {
    case bazarish::AliasProblem::eNameLength:
        return tr("An alias must be %1 to %2 characters long")
            .arg(bazarish::kAliasMinLength)
            .arg(bazarish::kAliasMaxLength);
    case bazarish::AliasProblem::eNameChars:
        return tr("An alias may contain only Latin letters and digits");
    case bazarish::AliasProblem::eWordLength:
    case bazarish::AliasProblem::eWordChars:
    case bazarish::AliasProblem::eWordEmpty:
        return tr("The word after the dash is 1 to %1 Latin letters and digits")
            .arg(bazarish::kAliasWordMaxLength);
    }
    throw std::logic_error("unknown alias problem");
}

QString SessionController::aliasWordProblem(const QString& word) const
{
    if (!bazarish::aliasWordProblem(word.trimmed().toStdString()).has_value()) {
        return {};
    }
    return tr("A word is up to %1 Latin letters and digits").arg(bazarish::kAliasWordMaxLength);
}

bool SessionController::typedAlias(const QString& typed) const
{
    return bazarish::linkQuery(typed.trimmed().toStdString(), bazarish::kInviteKind).empty();
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
    emit commands_.addByAlias(aliasChatPeer(alias), intro);
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
