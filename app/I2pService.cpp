// Bazarish project (c) 2026
#include "I2pService.hpp"

#include "Wire.hpp"

#include <QVariantMap>

#include "GatewayAddress.hpp"
#include "I2pRouter.hpp"
#include "SocksProxy.hpp"
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
#include <string>
#include <thread>

namespace bazarish::app {

namespace {

const QString kI2pScope = QStringLiteral("i2p");
// The slider passes through notches on its way: rebuild for where it stopped.
constexpr int kTunnelRebuildDelayMs = 1000;

bazarish::i2p::Privacy privacyForLevel(const int level)
{
    switch (level) {
        case kMinimalPrivacyLevel: return bazarish::i2p::Privacy::eMinimal;
        case kMiddlePrivacyLevel:  return bazarish::i2p::Privacy::eMiddle;
        default:                   return bazarish::i2p::Privacy::eMax;
    }
}
}  // namespace

I2pService::I2pService(QObject* parent)
    : QObject(parent)
{
    rebuildDelay_.setSingleShot(true);
    rebuildDelay_.setInterval(kTunnelRebuildDelayMs);
    connect(&rebuildDelay_, &QTimer::timeout, this, &I2pService::tunnelsNeedRebuild);
    connect(&commands_, &I2pCommands::setPrivacyLevel, this, &I2pService::setPrivacyLevel);
    connect(&commands_, &I2pCommands::setLoggingEnabled, this, &I2pService::setLoggingEnabled);
    connect(&commands_, &I2pCommands::checkAndSaveProxy, this, &I2pService::checkAndSaveProxy);
    connect(&commands_, &I2pCommands::checkAndSaveSam, this, &I2pService::checkAndSaveSam);
    connect(&commands_, &I2pCommands::useSam, this, &I2pService::useSam);
    connect(&commands_, &I2pCommands::useGateway, this, &I2pService::useGateway);
    connect(&commands_, &I2pCommands::checkAndSaveGateway, this,
        &I2pService::checkAndSaveGateway);
    connect(&commands_, &I2pCommands::skipGateway, this, &I2pService::skipGateway);
    connect(&commands_, &I2pCommands::useEmbedded, this, &I2pService::useEmbedded);
    connect(&commands_, &I2pCommands::refresh, this, &I2pService::refresh);

    state_.loggingEnabled = AppSettings::instance().i2pLogging();
    state_.privacyLevel = std::clamp(
        AppSettings::instance().i2pTunnelLength(), kMinimalPrivacyLevel, kMaxPrivacyLevel);
    state_.minKnownRouters = static_cast<int>(client::kMinKnownRouters);
    state_.proxyHost = QString::fromStdString(AppSettings::instance().i2pProxyHost());
    state_.proxyPort = AppSettings::instance().i2pProxyPort();
    state_.samHost = QString::fromStdString(AppSettings::instance().samHost());
    state_.samPort = AppSettings::instance().samPort();
    state_.samEnabled = AppSettings::instance().samEnabled();
    state_.gatewayAsked = AppSettings::instance().gatewayAsked();
    state_.gatewayEnabled = AppSettings::instance().gatewayEnabled();
    state_.gatewayAddress = QString::fromStdString(AppSettings::instance().gatewayAddress());
    transportAtStart_ = transport();
    client::setI2pSocksProxy(state_.proxyHost.toStdString(), state_.proxyPort);
    if (!state_.proxyHost.isEmpty() && transportAtStart_ == QLatin1String("embedded")) {
        bazarish::log::info("i2p: clearnet side goes through socks://{}:{}",
            state_.proxyHost.toStdString(), state_.proxyPort);
    }
    const std::optional<client::GatewayAddress> gateway
        = client::GatewayAddress::parse(state_.gatewayAddress.toStdString());
    state_.gatewayHost = gateway.has_value() ? QString::fromStdString(gateway->host) : QString();
    if (state_.gatewayEnabled) {
        if (gateway.has_value()) {
            client::setGatewayTransport(gateway.value(), AppSettings::instance().gatewayPin());
        } else {
            state_.gatewayEnabled = false;
        }
    }
    if (state_.samEnabled) {
        client::setSamTransport(state_.samHost.toStdString(), state_.samPort);
    }
    bazarish::i2p::setI2pLogging(state_.loggingEnabled);
    client::setTunnelPrivacy(privacyForLevel(state_.privacyLevel));
    refresh();
}

void I2pService::attach(Relay& relay)
{
    relay.sendFrom(kI2pScope, events_, kI2pLatches);
    relay.deliverTo(kI2pScope, commands_);
}

void I2pService::announce()
{
    emit events_.state(state_);
}

void I2pService::checkAndSaveSam(const QString& host, const int port)
{
    if (state_.samChecking) {
        return;
    }
    const QString wanted = host.trimmed();
    if (wanted.isEmpty() || port <= 0) {
        emit events_.samRefused(tr("Give the router's address first."));
        return;
    }
    state_.samChecking = true;
    announce();
    std::thread([this, wanted, port]() {
        QString failure;
        try {
            bazarish::sam::RouterAddress address;
            address.host = wanted.toStdString();
            address.controlPort = static_cast<std::uint16_t>(port);
            (void)bazarish::sam::probe(address);
        } catch (const std::exception& error) {
            failure = QString::fromUtf8(error.what());
        }
        QMetaObject::invokeMethod(
            this,
            [this, wanted, port, failure]() {
                state_.samChecking = false;
                if (!failure.isEmpty()) {
                    announce();
                    emit events_.samRefused(failure);
                    return;
                }
                state_.samHost = wanted;
                state_.samPort = port;
                AppSettings::instance().setSam(
                    state_.samEnabled, state_.samHost.toStdString(), state_.samPort);
                announce();
                emit events_.samSaved();
            },
            Qt::QueuedConnection);
    }).detach();
}

void I2pService::noteChoiceInForce()
{
    if (client::sharedI2pRouterIfRunning() == nullptr) {
        transportAtStart_ = transport();
    }
    state_.restartNeeded = transport() != transportAtStart_;
    announce();
}

void I2pService::useGateway(const bool on)
{
    if (on && state_.gatewayAddress.isEmpty()) {
        emit events_.gatewayRefused(tr("Save a gateway address first."));
        return;
    }
    AppSettings::instance().useGateway(on);
    state_.gatewayEnabled = AppSettings::instance().gatewayEnabled();
    state_.samEnabled = AppSettings::instance().samEnabled();
    noteChoiceInForce();
}

void I2pService::useSam(const bool on)
{
    if (on && state_.samHost.isEmpty()) {
        emit events_.samRefused(tr("Give the router's address first."));
        return;
    }
    state_.samEnabled = on;
    AppSettings::instance().setSam(on, state_.samHost.toStdString(), state_.samPort);
    state_.gatewayEnabled = AppSettings::instance().gatewayEnabled();
    noteChoiceInForce();
}

void I2pService::checkAndSaveProxy(const QString& host, const int port)
{
    if (state_.proxyChecking) {
        return;
    }
    const QString wantedHost = host.trimmed();
    if (wantedHost.isEmpty() || port <= 0) {
        saveProxy(QString(), 0);
        return;
    }
    state_.proxyChecking = true;
    announce();
    std::thread([this, wantedHost, port]() {
        const client::SocksCheck check
            = client::checkSocksProxy(wantedHost.toStdString(), port);
        QMetaObject::invokeMethod(
            this,
            [this, wantedHost, port, check]() {
                state_.proxyChecking = false;
                if (check.answer != client::SocksAnswer::eAccepted) {
                    if (!check.error.empty()) {
                        bazarish::log::warn("i2p: socks proxy {}:{}: {}",
                            wantedHost.toStdString(), port, check.error);
                    }
                    announce();
                    emit events_.proxyRefused(check.answer == client::SocksAnswer::eUnreachable
                            ? tr("Could not connect to this address.")
                            : check.answer == client::SocksAnswer::eNeedsAuthentication
                            ? tr("The proxy requires authentication.")
                            : tr("This is not a SOCKS5 proxy."));
                    return;
                }
                saveProxy(wantedHost, port);
            },
            Qt::QueuedConnection);
    }).detach();
}

void I2pService::saveProxy(const QString& host, const int port)
{
    state_.proxyHost = host;
    state_.proxyPort = port;
    AppSettings::instance().setI2pProxy(state_.proxyHost.toStdString(), state_.proxyPort);
    client::setI2pSocksProxy(state_.proxyHost.toStdString(), state_.proxyPort);
    announce();
    std::thread([]() { client::restartI2pRouter(); }).detach();
    refresh();
}

void I2pService::setLoggingEnabled(bool on)
{
    if (state_.loggingEnabled == on) {
        return;
    }
    state_.loggingEnabled = on;
    bazarish::i2p::setI2pLogging(on);
    AppSettings::instance().setI2pLogging(on);
    announce();
}

void I2pService::setPrivacyLevel(const int level)
{
    const int wanted = std::clamp(level, kMinimalPrivacyLevel, kMaxPrivacyLevel);
    if (state_.privacyLevel == wanted) {
        return;
    }
    state_.privacyLevel = wanted;
    client::setTunnelPrivacy(privacyForLevel(wanted));
    AppSettings::instance().setI2pTunnelLength(wanted);
    announce();
    rebuildDelay_.start();
}

void I2pService::checkAndSaveGateway(const QString& address)
{
    if (state_.gatewayChecking) {
        return;
    }
    const std::optional<client::GatewayAddress> parsed
        = client::GatewayAddress::parse(address.trimmed().toStdString());
    if (!parsed.has_value()) {
        emit events_.gatewayRefused(tr("That is not a gateway address. It looks like "
                                       "https://host/path#token."));
        return;
    }
    state_.gatewayChecking = true;
    announce();
    std::thread([this, parsed]() {
        const client::GatewayCheck check = client::checkGateway(parsed.value(), std::string());
        QMetaObject::invokeMethod(
            this,
            [this, parsed, check]() {
                state_.gatewayChecking = false;
                if (!check.ok) {
                    bazarish::log::warn("i2p: gateway check: {}", check.error);
                    announce();
                    emit events_.gatewayRefused(tr("Could not connect to this address."));
                    return;
                }
                AppSettings::instance().rememberGateway(parsed->toString(), check.pin);
                state_.gatewayAsked = true;
                state_.gatewayAddress = QString::fromStdString(parsed->toString());
                state_.gatewayHost = QString::fromStdString(parsed->host);
                announce();
                emit events_.gatewaySaved();
            },
            Qt::QueuedConnection);
    }).detach();
}

QString I2pService::transport() const
{
    if (state_.gatewayEnabled) {
        return QStringLiteral("gateway");
    }
    return state_.samEnabled ? QStringLiteral("sam") : QStringLiteral("embedded");
}

void I2pService::useEmbedded()
{
    AppSettings::instance().setSam(false, state_.samHost.toStdString(), state_.samPort);
    AppSettings::instance().useGateway(false);
    state_.samEnabled = false;
    state_.gatewayEnabled = false;
    noteChoiceInForce();
}

void I2pService::skipGateway()
{
    AppSettings::instance().skipGateway();
    state_.gatewayAsked = true;
    state_.gatewayEnabled = false;
    noteChoiceInForce();
}

void I2pService::refresh()
{
    bazarish::i2p::Router* const router = client::sharedI2pRouterIfRunning();
    const bool running = router != nullptr;
    bool ready = false;
    const bool ownEngine = !state_.samEnabled && !state_.gatewayEnabled;
    int knownRouters = ownEngine ? static_cast<int>(client::knownRouterCount(i2pRoot())) : 0;
    int floodfills = 0;
    int inboundTunnels = 0;
    int outboundTunnels = 0;
    QStringList transports;
    QVariantList destinations;
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
            row[QStringLiteral("localRoute")] = dest.localRoute;
            row[QStringLiteral("countsKnown")] = router->capabilities().destinationCounters;
            destinations << row;
        }
    }
    I2pState now = state_;
    now.running = running;
    now.ready = ready;
    now.knownRouters = knownRouters;
    now.floodfills = floodfills;
    now.inboundTunnels = inboundTunnels;
    now.outboundTunnels = outboundTunnels;
    now.transports = transports;
    now.destinations = destinations;
    if (now == state_) {
        return;
    }
    state_ = now;
    announce();
}

}  // namespace bazarish::app
