// Bazarish project (c) 2026
#pragma once

#include "I2pEvents.hpp"

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>

namespace bazarish::app {

class Relay;

class I2pController : public QObject {
    Q_OBJECT
    // libi2pd's own logging. OFF by default (fully suppressed); a debugging aid.
    Q_PROPERTY(bool loggingEnabled READ loggingEnabled WRITE setLoggingEnabled NOTIFY loggingChanged)
    Q_PROPERTY(int privacyLevel READ privacyLevel WRITE setPrivacyLevel NOTIFY privacyLevelChanged)
    Q_PROPERTY(bool running READ running NOTIFY statusChanged)
    Q_PROPERTY(bool ready READ ready NOTIFY statusChanged)
    Q_PROPERTY(bool tunnelsBuilding READ tunnelsBuilding NOTIFY statusChanged)
    Q_PROPERTY(int knownRouters READ knownRouters NOTIFY statusChanged)
    Q_PROPERTY(int minKnownRouters READ minKnownRouters NOTIFY statusChanged)
    Q_PROPERTY(int floodfills READ floodfills NOTIFY statusChanged)
    Q_PROPERTY(int inboundTunnels READ inboundTunnels NOTIFY statusChanged)
    Q_PROPERTY(int outboundTunnels READ outboundTunnels NOTIFY statusChanged)
    Q_PROPERTY(QStringList transports READ transports NOTIFY statusChanged)
    Q_PROPERTY(QVariantList destinations READ destinations NOTIFY statusChanged)
    Q_PROPERTY(QString proxyHost READ proxyHost NOTIFY proxyChanged)
    Q_PROPERTY(int proxyPort READ proxyPort NOTIFY proxyChanged)
    Q_PROPERTY(bool proxyChecking READ proxyChecking NOTIFY proxyCheckingChanged)
    Q_PROPERTY(bool samEnabled READ samEnabled NOTIFY samChanged)
    Q_PROPERTY(bool samChecking READ samChecking NOTIFY samChanged)
    Q_PROPERTY(QString samHost READ samHost NOTIFY samChanged)
    Q_PROPERTY(int samPort READ samPort NOTIFY samChanged)
    Q_PROPERTY(bool gatewayAsked READ gatewayAsked NOTIFY gatewayChanged)
    Q_PROPERTY(bool gatewayEnabled READ gatewayEnabled NOTIFY gatewayChanged)
    Q_PROPERTY(QString gatewayAddress READ gatewayAddress NOTIFY gatewayChanged)
    Q_PROPERTY(bool gatewayChecking READ gatewayChecking NOTIFY gatewayChanged)
    Q_PROPERTY(QString gatewayHost READ gatewayHost NOTIFY gatewayChanged)
    Q_PROPERTY(QString transport READ transport NOTIFY transportChanged)
public:
    explicit I2pController(QObject* parent = nullptr);

    void attach(Relay& relay);

    bool loggingEnabled() const { return state_.loggingEnabled; }
    int privacyLevel() const { return state_.privacyLevel; }
    void setPrivacyLevel(int level);
    void setLoggingEnabled(bool on);
    bool running() const { return state_.running; }
    bool ready() const { return state_.ready; }
    // No destination can carry anything yet, and one is being raised.
    bool tunnelsBuilding() const;
    int knownRouters() const { return state_.knownRouters; }
    int minKnownRouters() const { return state_.minKnownRouters; }
    QString proxyHost() const { return state_.proxyHost; }
    int proxyPort() const { return state_.proxyPort; }
    bool proxyChecking() const { return state_.proxyChecking; }
    Q_INVOKABLE void checkAndSaveProxy(const QString& host, int port);
    bool samEnabled() const { return state_.samEnabled; }
    bool samChecking() const { return state_.samChecking; }
    QString samHost() const { return state_.samHost; }
    int samPort() const { return state_.samPort; }
    Q_INVOKABLE void checkAndSaveSam(const QString& host, int port);
    Q_INVOKABLE void useSam(bool on);
    Q_INVOKABLE void useGateway(bool on);
    int floodfills() const { return state_.floodfills; }
    int inboundTunnels() const { return state_.inboundTunnels; }
    int outboundTunnels() const { return state_.outboundTunnels; }
    QStringList transports() const { return state_.transports; }
    QVariantList destinations() const { return state_.destinations; }

    bool gatewayAsked() const { return state_.gatewayAsked; }
    bool gatewayEnabled() const { return state_.gatewayEnabled; }
    QString gatewayAddress() const { return state_.gatewayAddress; }
    bool gatewayChecking() const { return state_.gatewayChecking; }
    QString gatewayHost() const { return state_.gatewayHost; }
    QString transport() const;
    Q_PROPERTY(bool restartNeeded READ restartNeeded NOTIFY transportChanged)
    bool restartNeeded() const { return state_.restartNeeded; }
    Q_INVOKABLE void checkAndSaveGateway(const QString& address);
    Q_INVOKABLE void skipGateway();
    Q_INVOKABLE void useEmbedded();

    Q_INVOKABLE void refresh();

signals:
    void loggingChanged();
    void privacyLevelChanged();
    void statusChanged();
    void proxyChanged();
    void proxyCheckingChanged();
    void proxyRefused(const QString& reason);
    void samChanged();
    void gatewayChanged();
    void transportChanged();
    void gatewaySaved();
    void gatewayRefused(const QString& reason);
    void samSaved();
    void samRefused(const QString& reason);

private:
    void take(const I2pState& state);

    I2pCommands commands_;
    I2pEvents events_;
    I2pState state_;
};

}  // namespace bazarish::app
