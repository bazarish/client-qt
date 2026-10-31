// Bazarish project (c) 2026
#pragma once

#include "Wire.hpp"

#include <QDataStream>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>

namespace bazarish::app {

inline constexpr int kMinimalPrivacyLevel = 0;
inline constexpr int kMiddlePrivacyLevel = 1;
inline constexpr int kMaxPrivacyLevel = 2;

struct I2pState {
    bool loggingEnabled = false;
    int privacyLevel = 0;
    bool running = false;
    bool ready = false;
    int knownRouters = 0;
    int minKnownRouters = 0;
    int floodfills = 0;
    int inboundTunnels = 0;
    int outboundTunnels = 0;
    QStringList transports;
    QVariantList destinations;
    QString proxyHost;
    int proxyPort = 0;
    bool proxyChecking = false;
    bool samEnabled = false;
    bool samChecking = false;
    QString samHost;
    int samPort = 0;
    bool gatewayAsked = false;
    bool gatewayEnabled = false;
    bool gatewayChecking = false;
    QString gatewayAddress;
    QString gatewayHost;
    bool restartNeeded = false;

    bool operator==(const I2pState&) const = default;
};

inline QDataStream& operator<<(QDataStream& out, const I2pState& state)
{
    return out << state.loggingEnabled << qint32(state.privacyLevel) << state.running
               << state.ready << qint32(state.knownRouters) << qint32(state.minKnownRouters)
               << qint32(state.floodfills) << qint32(state.inboundTunnels)
               << qint32(state.outboundTunnels) << state.transports << state.destinations
               << state.proxyHost << qint32(state.proxyPort) << state.proxyChecking
               << state.samEnabled << state.samChecking << state.samHost
               << qint32(state.samPort) << state.gatewayAsked << state.gatewayEnabled
               << state.gatewayChecking << state.gatewayAddress << state.gatewayHost
               << state.restartNeeded;
}

inline QDataStream& operator>>(QDataStream& in, I2pState& state)
{
    qint32 privacyLevel = 0;
    qint32 knownRouters = 0;
    qint32 minKnownRouters = 0;
    qint32 floodfills = 0;
    qint32 inboundTunnels = 0;
    qint32 outboundTunnels = 0;
    qint32 proxyPort = 0;
    qint32 samPort = 0;
    in >> state.loggingEnabled >> privacyLevel >> state.running >> state.ready >> knownRouters
        >> minKnownRouters >> floodfills >> inboundTunnels >> outboundTunnels >> state.transports
        >> state.destinations >> state.proxyHost >> proxyPort >> state.proxyChecking
        >> state.samEnabled >> state.samChecking >> state.samHost >> samPort
        >> state.gatewayAsked >> state.gatewayEnabled >> state.gatewayChecking
        >> state.gatewayAddress >> state.gatewayHost >> state.restartNeeded;
    state.privacyLevel = privacyLevel;
    state.knownRouters = knownRouters;
    state.minKnownRouters = minKnownRouters;
    state.floodfills = floodfills;
    state.inboundTunnels = inboundTunnels;
    state.outboundTunnels = outboundTunnels;
    state.proxyPort = proxyPort;
    state.samPort = samPort;
    return in;
}

class I2pCommands : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;

signals:
    void setPrivacyLevel(int level);
    void setLoggingEnabled(bool on);
    void checkAndSaveProxy(const QString& host, int port);
    void checkAndSaveSam(const QString& host, int port);
    void useSam(bool on);
    void useGateway(bool on);
    void checkAndSaveGateway(const QString& address);
    void skipGateway();
    void useEmbedded();
    void refresh();
};

inline constexpr Latch kI2pLatches[] = {{"state"}};

class I2pEvents : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;

signals:
    void state(const I2pState& state);
    void proxyRefused(const QString& reason);
    void gatewaySaved();
    void gatewayRefused(const QString& reason);
    void samSaved();
    void samRefused(const QString& reason);
};

}  // namespace bazarish::app
