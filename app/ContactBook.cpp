// Bazarish project (c) 2026
#include "ContactBook.hpp"

#include "SessionShared.hpp"

namespace bazarish::app {

namespace {
constexpr int kShortFingerprintAbove = 14;
constexpr int kShortFingerprintHead = 8;
constexpr int kShortFingerprintTail = 4;
}  // namespace

void ContactBook::reset(const QVector<ContactState>& contacts, const QStringList& blocked)
{
    fingerprints_.clear();
    states_.clear();
    for (const ContactState& contact : contacts) {
        fingerprints_ << contact.fingerprint;
        states_.insert(contact.fingerprint, contact);
    }
    blocked_ = blocked;
}

QString ContactBook::peerName(const QString& id) const
{
    if (!id.isEmpty() && id == ownFingerprint_) {
        return savedChatName();
    }
    const QString name = states_.contains(id) ? states_.value(id).name : pendingNames_.value(id);
    if (!name.isEmpty()) {
        return name;
    }
    if (hasAliasSigil(id)) {
        return id.mid(1);
    }
    return shortFingerprint(id);
}

void ContactBook::remove(const QString& fingerprint)
{
    fingerprints_.removeAll(fingerprint);
    states_.remove(fingerprint);
}

QStringList ContactBook::accepting() const
{
    QStringList out;
    for (const ContactState& contact : states_) {
        if (contact.request == ContactState::eAccepting) {
            out << contact.fingerprint;
        }
    }
    return out;
}

void ContactBook::setBlocked(const QString& peer, const bool blocked)
{
    if (!blocked) {
        blocked_.removeAll(peer);
        return;
    }
    if (!blocked_.contains(peer)) {
        blocked_.push_back(peer);
    }
}

void ContactBook::notePendingName(const QString& peer, const QString& name)
{
    pendingNames_.insert(peer, name);
}

QString ContactBook::savedChatName()
{
    return tr("Saved messages");
}

QString ContactBook::shortFingerprint(const QString& fingerprint)
{
    if (fingerprint.size() <= kShortFingerprintAbove) {
        return fingerprint;
    }
    return fingerprint.left(kShortFingerprintHead) + QStringLiteral("…")
        + fingerprint.right(kShortFingerprintTail);
}

}  // namespace bazarish::app
