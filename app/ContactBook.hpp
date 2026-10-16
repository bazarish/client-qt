// Bazarish project (c) 2026
#pragma once

#include "AccountEvents.hpp"

#include <QCoreApplication>
#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>

namespace bazarish::app {

class ContactBook {
    Q_DECLARE_TR_FUNCTIONS(ContactBook)
public:
    void reset(const QVector<ContactState>& contacts, const QStringList& blocked);
    void setOwnFingerprint(const QString& fingerprint) { ownFingerprint_ = fingerprint; }
    QString ownFingerprint() const { return ownFingerprint_; }

    QString peerName(const QString& id) const;
    ContactState state(const QString& fingerprint) const { return states_.value(fingerprint); }
    bool has(const QString& fingerprint) const { return states_.contains(fingerprint); }
    ContactState& edit(const QString& fingerprint) { return states_[fingerprint]; }
    void remove(const QString& fingerprint);
    QStringList fingerprints() const { return fingerprints_; }
    QStringList accepting() const;

    bool isBlocked(const QString& peer) const { return blocked_.contains(peer); }
    void setBlocked(const QString& peer, bool blocked);
    QStringList blocked() const { return blocked_; }

    void notePendingName(const QString& peer, const QString& name);

    static QString savedChatName();
    static QString shortFingerprint(const QString& fingerprint);

private:
    QString ownFingerprint_;
    QStringList fingerprints_;
    QHash<QString, ContactState> states_;
    QStringList blocked_;
    QHash<QString, QString> pendingNames_;
};

}  // namespace bazarish::app
