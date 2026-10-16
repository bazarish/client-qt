// Bazarish project (c) 2026
#pragma once

#include "AccountEvents.hpp"

#include <QDataStream>
#include <QObject>
#include <QString>
#include <QVector>

namespace bazarish::app {

struct AccountListRow {
    QString id;
    QString name;
    QString fingerprint;
    bool encrypted = false;
    bool open = false;
};

struct AccountRow {
    QString id;
    QString name;
    QString fingerprint;
    bool open = false;
    bool active = false;
    bool online = false;
    bool connected = false;
    bool encrypted = false;
    int unread = 0;
    QString activeFacade;
};

inline QDataStream& operator<<(QDataStream& out, const AccountListRow& row)
{
    return out << row.id << row.name << row.fingerprint << row.encrypted << row.open;
}

inline QDataStream& operator>>(QDataStream& in, AccountListRow& row)
{
    return in >> row.id >> row.name >> row.fingerprint >> row.encrypted >> row.open;
}

inline QDataStream& operator<<(QDataStream& out, const AccountRow& row)
{
    return out << row.id << row.name << row.fingerprint << row.open << row.active << row.online
               << row.connected << row.encrypted << static_cast<qint32>(row.unread)
               << row.activeFacade;
}

inline QDataStream& operator>>(QDataStream& in, AccountRow& row)
{
    qint32 unread = 0;
    in >> row.id >> row.name >> row.fingerprint >> row.open >> row.active >> row.online
        >> row.connected >> row.encrypted >> unread >> row.activeFacade;
    row.unread = unread;
    return in;
}

inline constexpr Latch kAppLatches[] = {
    {"settings"},
    {"accountList"},
    {"accounts"},
    {"activeChanged"},
    {"ringing"},
    {"call"},
    {"speakerOn"},
    {"pairing"},
    {"deleting"},
};

class AppEvents : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;

signals:
    void started();
    void accountList(const QVector<AccountListRow>& rows);
    void accounts(const QVector<AccountRow>& rows);
    void sessionStarted(const QString& id);
    void sessionEnded(const QString& id);
    void accountReady(const QString& id);
    void activeChanged(const QString& id);
    void needPassphrase(const QString& id, const QString& name);
    void unlockFailed(const QString& error);
    void accountUnlocked(const QString& id);
    void accountOpenFailed(const QString& error);
    void createFailed(const QString& error);
    void accountDeleteFailed(const QString& id, const QString& error, bool profileNotOpened);
    void accountDeleteNeedsUnlock(const QString& id, const QString& name);
    void deleting(const QString& id);
    void pairing(const QString& status, double progress, bool needsCode, bool active);
    void pairingFinished(bool ok);
    void settings(bool notifications, bool backgroundTasks);
    void ringing(const QString& account, const QString& peer, const QString& fingerprint,
        const QString& accountName);
    void ringLevel(double level);
    void call(bool live);
    void speakerOn(bool on);
    void portableChanged();
    void restartRequired(const QString& message);
    void opChanged(const OperationRow& row);
    void opRemoved(const QString& id);
};

}  // namespace bazarish::app
