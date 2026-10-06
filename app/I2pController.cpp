// Bazarish project (c) 2026
#include "I2pController.hpp"

#include <QVariantMap>

#include "GatewayAddress.hpp"
#include "I2pRouter.hpp"
#include "AccountManager.hpp"
#include "AppSettings.hpp"

#include <bazarish/I2p.hpp>
#include <bazarish/Sam.hpp>

#pragma push_macro("emit")
#undef emit
#include <bazarish/Log.hpp>
#pragma pop_macro("emit")

#include <algorithm>
#include <optional>
#include <thread>
#include <cstdlib>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

namespace bazarish::app {

namespace {
std::filesystem::path i2pRoot()
{
    return appRoot() / "i2p";
}

bazarish::i2p::Privacy privacyForLevel(const int level)
{
    switch (level) {
        case kMinimalPrivacyLevel: return bazarish::i2p::Privacy::eMinimal;
        case kMiddlePrivacyLevel:  return bazarish::i2p::Privacy::eMiddle;
        default:                   return bazarish::i2p::Privacy::eMax;
    }
}
}  // namespace

I2pController::I2pController(QObject* parent)
    : QObject(parent)
{
    transportAtStart_ = AppSettings::instance().gatewayEnabled()
        ? QStringLiteral("gateway")
        : (AppSettings::instance().samEnabled() ? QStringLiteral("sam")
                                                : QStringLiteral("embedded"));
    // libi2pd logging is off by default: fully silent.
    loggingEnabled_ = AppSettings::instance().i2pLogging();
    privacyLevel_ = std::clamp(
        AppSettings::instance().i2pTunnelLength(), kMinimalPrivacyLevel, kMaxPrivacyLevel);
    {
        proxyHost_ = QString::fromStdString(AppSettings::instance().i2pProxyHost());
        proxyPort_ = AppSettings::instance().i2pProxyPort();
        client::setI2pSocksProxy(proxyHost_.toStdString(), proxyPort_);
        if (!proxyHost_.isEmpty()) {
            bazarish::log::info("i2p: clearnet side goes through socks://{}:{}",
                proxyHost_.toStdString(), proxyPort_);
        }
    }
    samHost_ = QString::fromStdString(AppSettings::instance().samHost());
    samPort_ = AppSettings::instance().samPort();
    samEnabled_ = AppSettings::instance().samEnabled();
    gatewayAsked_ = AppSettings::instance().gatewayAsked();
    gatewayEnabled_ = AppSettings::instance().gatewayEnabled();
    gatewayAddress_ = QString::fromStdString(AppSettings::instance().gatewayAddress());
    if (gatewayEnabled_) {
        const std::optional<client::GatewayAddress> parsed
            = client::GatewayAddress::parse(gatewayAddress_.toStdString());
        if (parsed.has_value()) {
            client::setGatewayTransport(parsed.value(), AppSettings::instance().gatewayPin());
        } else {
            gatewayEnabled_ = false;
        }
    }
    if (samEnabled_) {
        client::setSamTransport(samHost_.toStdString(), samPort_);
    }
    client::setI2pEnabled(true);
    bazarish::i2p::setI2pLogging(loggingEnabled_);
    client::setTunnelPrivacy(privacyForLevel(privacyLevel_));
    reconcileRouter();
    refresh();
}

bool I2pController::samReachable(const QString& host, const int port) const
{
    try {
        bazarish::sam::RouterAddress address;
        address.host = host.trimmed().toStdString();
        address.controlPort = static_cast<std::uint16_t>(port);
        (void)bazarish::sam::probe(address);
        return true;
    } catch (const std::exception& error) {
        bazarish::log::info("i2p: no SAM router at {}:{}: {}", host.toStdString(), port,
            error.what());
        return false;
    }
}

void I2pController::saveSam(const QString& host, const int port)
{
    samHost_ = host.trimmed();
    samPort_ = port;
    AppSettings::instance().setSam(samEnabled_, samHost_.toStdString(), samPort_);
    emit samChanged();
}

void I2pController::noteChoiceInForce()
{
    if (client::sharedI2pRouterIfRunning() == nullptr) {
        transportAtStart_ = transport();
    }
}

void I2pController::useGateway(const bool on)
{
    if (on && gatewayAddress_.isEmpty()) {
        emit gatewayRefused(tr("Save a gateway address first."));
        return;
    }
    AppSettings::instance().useGateway(on);
    gatewayEnabled_ = AppSettings::instance().gatewayEnabled();
    samEnabled_ = AppSettings::instance().samEnabled();
    noteChoiceInForce();
    emit samChanged();
    emit gatewayChanged();
    emit transportChanged();
}

void I2pController::useSam(const bool on)
{
    if (on && samHost_.isEmpty()) {
        emit gatewayRefused(tr("Give the router's address first."));
        return;
    }
    samEnabled_ = on;
    AppSettings::instance().setSam(on, samHost_.toStdString(), samPort_);
    gatewayEnabled_ = AppSettings::instance().gatewayEnabled();
    noteChoiceInForce();
    emit samChanged();
    emit gatewayChanged();
    emit transportChanged();
}

void I2pController::saveProxy(const QString& host, const int port, const bool restartNow)
{
    const QString wantedHost = host.trimmed();
    const bool clearing = wantedHost.isEmpty() || port <= 0;
    proxyHost_ = clearing ? QString() : wantedHost;
    proxyPort_ = clearing ? 0 : port;
    AppSettings::instance().setI2pProxy(proxyHost_.toStdString(), proxyPort_);
    client::setI2pSocksProxy(proxyHost_.toStdString(), proxyPort_);
    emit proxyChanged();
    if (restartNow) {
        const std::filesystem::path dataDir = i2pRoot();
        std::thread([dataDir]() { client::restartI2pRouter(dataDir); }).detach();
    }
    refresh();
}

void I2pController::reconcileRouter()
{
    const std::filesystem::path dir = i2pRoot();
    std::thread([dir]() { client::reconcileI2pRouter(dir); }).detach();
}

void I2pController::setLoggingEnabled(bool on)
{
    if (loggingEnabled_ == on) {
        return;
    }
    loggingEnabled_ = on;
    bazarish::i2p::setI2pLogging(on);
    AppSettings::instance().setI2pLogging(on);
    emit loggingChanged();
}

void I2pController::setPrivacyLevel(const int level)
{
    const int wanted = std::clamp(level, kMinimalPrivacyLevel, kMaxPrivacyLevel);
    if (privacyLevel_ == wanted) {
        return;
    }
    privacyLevel_ = wanted;
    client::setTunnelPrivacy(privacyForLevel(wanted));
    AppSettings::instance().setI2pTunnelLength(wanted);
    emit privacyLevelChanged();
}

int I2pController::minKnownRouters() const
{
    return static_cast<int>(client::kMinKnownRouters);
}

void I2pController::checkAndSaveGateway(const QString& address)
{
    if (gatewayChecking_) {
        return;
    }
    const std::optional<client::GatewayAddress> parsed
        = client::GatewayAddress::parse(address.trimmed().toStdString());
    if (!parsed.has_value()) {
        emit gatewayRefused(tr("That is not a gateway address. It looks like "
                               "https://host/path#token."));
        return;
    }
    gatewayChecking_ = true;
    emit gatewayChanged();
    std::thread([this, parsed]() {
        const client::GatewayCheck check = client::checkGateway(parsed.value(), std::string());
        QMetaObject::invokeMethod(
            this,
            [this, parsed, check]() {
                gatewayChecking_ = false;
                if (!check.ok) {
                    emit gatewayChanged();
                    emit gatewayRefused(QString::fromStdString(check.error));
                    return;
                }
                AppSettings::instance().rememberGateway(parsed->toString(), check.pin);
                gatewayAsked_ = true;
                gatewayAddress_ = QString::fromStdString(parsed->toString());
                emit gatewayChanged();
                emit gatewaySaved();
            },
            Qt::QueuedConnection);
    }).detach();
}

QString I2pController::gatewayHost() const
{
    const std::optional<client::GatewayAddress> parsed
        = client::GatewayAddress::parse(gatewayAddress_.toStdString());
    return parsed.has_value() ? QString::fromStdString(parsed->host) : QString();
}

QString I2pController::transport() const
{
    if (gatewayEnabled_) {
        return QStringLiteral("gateway");
    }
    return samEnabled_ ? QStringLiteral("sam") : QStringLiteral("embedded");
}

void I2pController::useEmbedded()
{
    AppSettings::instance().setSam(false, samHost_.toStdString(), samPort_);
    AppSettings::instance().useGateway(false);
    samEnabled_ = false;
    gatewayEnabled_ = false;
    noteChoiceInForce();
    emit samChanged();
    emit gatewayChanged();
    emit transportChanged();
}

void I2pController::skipGateway()
{
    AppSettings::instance().skipGateway();
    gatewayAsked_ = true;
    gatewayEnabled_ = false;
    noteChoiceInForce();
    emit gatewayChanged();
    emit transportChanged();
}

void I2pController::clearGateway()
{
    AppSettings::instance().skipGateway();
    gatewayEnabled_ = false;
    noteChoiceInForce();
    emit gatewayChanged();
    emit transportChanged();
}

void I2pController::refresh()
{
    bazarish::i2p::Router* const router = client::sharedI2pRouterIfRunning();
    const bool running = router != nullptr;
    bool ready = false;
    const bool ownEngine = !samEnabled_ && !gatewayEnabled_;
    int knownRouters = ownEngine ? static_cast<int>(client::knownRouterCount(i2pRoot())) : 0;
    int floodfills = 0;
    int inboundTunnels = 0;
    int outboundTunnels = 0;
    QStringList transports;
    QVariantList destinations;
    QString proxyNtcp2;
    if (const std::optional<bazarish::i2p::ProxyState> proxy = client::i2pProxyState();
        proxy.has_value()) {
        proxyNtcp2 = QString::fromStdString(proxy->ntcp2);
    }
    if (running) {
        ready = router->ready();
    }
    if (running && router->capabilities().routerCounters) {
        knownRouters = router->knownRouters();
        floodfills = router->floodfills();
        inboundTunnels = router->inboundTunnels();
        outboundTunnels = router->outboundTunnels();
        for (const bazarish::i2p::TransportPeer& peer : router->transportPeers()) {
            QString row = QString::fromStdString(peer.transport)
                + (peer.outbound ? " · " : " · incoming · ");
            if (!peer.endpoint.empty()) {
                row += QString::fromStdString(peer.endpoint) + " · ";
            }
            row += QString::fromStdString(peer.ident);
            transports << row;
        }
        transports.sort();
    }
    if (running) {
        const bool manyAccounts = client::AccountManager(accountsRoot()).list().size() > 1;
        for (const bazarish::i2p::LocalDestination& dest : router->localDestinations()) {
            QVariantMap row;
            const QString what = dest.label.empty() ? QStringLiteral("Destination")
                                                    : QString::fromStdString(dest.label);
            row[QStringLiteral("label")] = (manyAccounts && !dest.owner.empty())
                ? (QString::fromStdString(dest.owner) + QStringLiteral(": ") + what)
                : what;
            row[QStringLiteral("host")] = QString::fromStdString(dest.host);
            row[QStringLiteral("state")] = dest.closing ? QStringLiteral("closing")
                : dest.ready
                ? (dest.published ? QStringLiteral("published") : QStringLiteral("ready"))
                : QStringLiteral("building");
            row[QStringLiteral("tunnelsIn")] = dest.inboundTunnels;
            row[QStringLiteral("tunnelsOut")] = dest.outboundTunnels;
            row[QStringLiteral("leaseSets")] = dest.remoteLeaseSets;
            row[QStringLiteral("countsKnown")] = router->capabilities().destinationCounters;
            destinations << row;
        }
    }
    if (running == running_ && ready == ready_ && knownRouters == knownRouters_
        && floodfills == floodfills_ && inboundTunnels == inboundTunnels_
        && outboundTunnels == outboundTunnels_ && transports == transports_
        && destinations == destinations_ && proxyNtcp2 == proxyNtcp2_) {
        return;
    }
    proxyNtcp2_ = proxyNtcp2;
    running_ = running;
    ready_ = ready;
    knownRouters_ = knownRouters;
    floodfills_ = floodfills;
    inboundTunnels_ = inboundTunnels;
    outboundTunnels_ = outboundTunnels;
    transports_ = transports;
    destinations_ = destinations;
    emit statusChanged();
}

}  // namespace bazarish::app
