// Bazarish project (c) 2026
#include "I2pService.hpp"
#include "Background.hpp"

#include "Wire.hpp"

#include <QStringList>
#include <QVariantList>
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

#include <functional>
#include <optional>
#include <string>
#include <stdexcept>
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

bazarish::i2p::I2pLogLevel i2pLogLevelFor(const bool loggingEnabled)
{
    return loggingEnabled ? bazarish::i2p::I2pLogLevel::eWarn : bazarish::i2p::I2pLogLevel::eNone;
}

bool manyAccounts()
{
    return client::AccountManager(accountsRoot()).list().size() > 1;
}

QString destinationLabel(const bazarish::i2p::LocalDestination& dest, const bool manyAccounts)
{
    const QString what = dest.label.empty() ? QStringLiteral("Destination")
                                            : QString::fromStdString(dest.label);
    return (manyAccounts && !dest.accountLabel.empty())
        ? (QString::fromStdString(dest.accountLabel) + QStringLiteral(": ") + what)
        : what;
}

QString destinationState(const bazarish::i2p::LocalDestination& dest)
{
    return dest.closing ? QStringLiteral("closing")
        : dest.ready
        ? (dest.published ? QStringLiteral("published") : QStringLiteral("ready"))
        : QStringLiteral("building");
}

QString tunnelStateName(const bazarish::i2p::TunnelState state)
{
    switch (state) {
        case bazarish::i2p::TunnelState::eBuilding:    return QStringLiteral("building");
        case bazarish::i2p::TunnelState::eEstablished: return QStringLiteral("established");
        case bazarish::i2p::TunnelState::eExpiring:    return QStringLiteral("expiring");
        case bazarish::i2p::TunnelState::eFailed:      return QStringLiteral("failed");
    }
    return QStringLiteral("failed");
}

QVariantMap tunnelRow(const bazarish::i2p::TunnelDetail& tunnel)
{
    QStringList hops;
    for (const std::string& hop : tunnel.hops) {
        hops << QString::fromStdString(hop);
    }
    QVariantMap row;
    row[QStringLiteral("hops")] = hops;
    row[QStringLiteral("state")] = tunnelStateName(tunnel.state);
    if (tunnel.latencyMs.has_value()) {
        row[QStringLiteral("latencyMs")] = tunnel.latencyMs.value();
    }
    row[QStringLiteral("bytes")] = static_cast<qint64>(tunnel.bytes);
    return row;
}

QVariantMap detailMap(const bazarish::i2p::DestinationDetail& detail, const QSet<QString>& refreshing)
{
    QVariantList inbound;
    QVariantList outbound;
    for (const bazarish::i2p::TunnelDetail& tunnel : detail.tunnels) {
        (tunnel.inbound ? inbound : outbound) << tunnelRow(tunnel);
    }
    QVariantList leaseSets;
    for (const bazarish::i2p::RemoteLeaseSet& leaseSet : detail.leaseSets) {
        QVariantMap row;
        row[QStringLiteral("host")] = QString::fromStdString(leaseSet.host);
        row[QStringLiteral("expiresIn")] = static_cast<qint64>(leaseSet.expiresIn.count());
        row[QStringLiteral("leases")] = leaseSet.leases;
        leaseSets << row;
    }
    QVariantMap map;
    map[QStringLiteral("host")] = QString::fromStdString(detail.destination.host);
    map[QStringLiteral("label")] = destinationLabel(detail.destination, manyAccounts());
    map[QStringLiteral("state")] = destinationState(detail.destination);
    map[QStringLiteral("inbound")] = inbound;
    map[QStringLiteral("outbound")] = outbound;
    map[QStringLiteral("leaseSets")] = leaseSets;
    map[QStringLiteral("refreshing")] = QStringList(refreshing.cbegin(), refreshing.cend());
    return map;
}

bool canDetail(const std::optional<std::reference_wrapper<bazarish::i2p::Router>>& router)
{
    return router.has_value() && router->get().capabilities().destinationDetail;
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
    connect(&commands_, &I2pCommands::inspectDestination, this, &I2pService::inspectDestination);
    connect(&commands_, &I2pCommands::refreshLeaseSet, this, &I2pService::refreshLeaseSet);

    state_.loggingEnabled = AppSettings::instance().i2pLogging();
    state_.privacyLevel = boundedPrivacyLevel(AppSettings::instance().i2pTunnelLength());
    state_.minKnownRouters = static_cast<int>(client::kMinKnownRouters);
    state_.proxyHost = QString::fromStdString(AppSettings::instance().i2pProxyHost());
    state_.proxyPort = AppSettings::instance().i2pProxyPort();
    state_.samHost = QString::fromStdString(AppSettings::instance().samHost());
    state_.samPort = AppSettings::instance().samPort();
    state_.samEnabled = AppSettings::instance().samEnabled();
    state_.gatewayAsked = AppSettings::instance().gatewayAsked();
    state_.gatewayEnabled = AppSettings::instance().gatewayEnabled();
    transportAtStart_ = transport();
    client::setI2pSocksProxy(state_.proxyHost.toStdString(), state_.proxyPort);
    if (!state_.proxyHost.isEmpty() && transportAtStart_ == QLatin1String("embedded")) {
        bazarish::log::info("i2p: clearnet side goes through socks://{}:{}",
            state_.proxyHost.toStdString(), state_.proxyPort);
    }
    const std::optional<client::GatewayAddress> gateway
        = client::GatewayAddress::parse(AppSettings::instance().gatewayAddress());
    state_.gatewayAddress = gateway.has_value() ? QString::fromStdString(gateway->link()) : QString();
    state_.gatewayHost = gateway.has_value() ? QString::fromStdString(gateway->host) : QString();
    gatewayAtStart_ = state_.gatewayAddress;
    if (!applyTransport()) {
        throw std::logic_error("a router ran before the I2P settings were read");
    }
    bazarish::i2p::setI2pLogLevel(i2pLogLevelFor(state_.loggingEnabled));
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
        emit events_.samRefused(tr("Give the router's address first"));
        return;
    }
    state_.samChecking = true;
    announce();
    inBackground([this, wanted, port]() {
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
    });
}

bool I2pService::applyTransport()
{
    const std::optional<client::GatewayAddress> gateway
        = client::GatewayAddress::parse(AppSettings::instance().gatewayAddress());
    const bool gatewayWanted = state_.gatewayEnabled;
    const bool samWanted = state_.samEnabled;
    const std::string samHost = state_.samHost.toStdString();
    const int samPort = state_.samPort;
    return client::applyI2pTransport([gateway, gatewayWanted, samWanted, samHost, samPort]() {
        if (gatewayWanted && gateway.has_value()) {
            client::setGatewayTransport(gateway.value());
        } else if (gatewayWanted) {
            bazarish::log::warn("i2p: the saved gateway address is not usable");
            client::refuseI2pTransport(
                "the saved gateway address is not usable: add the gateway again");
        } else {
            client::clearGatewayTransport();
        }
        client::setSamTransport(samWanted ? samHost : std::string(), samWanted ? samPort : 0);
    });
}

void I2pService::noteChoiceInForce()
{
    if (applyTransport()) {
        transportAtStart_ = transport();
        gatewayAtStart_ = state_.gatewayAddress;
    }
    state_.restartNeeded = transport() != transportAtStart_
        || (transport() == QLatin1String("gateway") && state_.gatewayAddress != gatewayAtStart_);
    if (state_.restartNeeded) {
        client::retireI2pRouter();
    }
    announce();
}

void I2pService::useGateway(const bool on)
{
    if (on && state_.gatewayAddress.isEmpty()) {
        emit events_.gatewayRefused(tr("Save a gateway address first"));
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
        emit events_.samRefused(tr("Give the router's address first"));
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
    inBackground([this, wantedHost, port]() {
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
                            ? tr("Could not connect to this address")
                            : check.answer == client::SocksAnswer::eNeedsAuthentication
                            ? tr("The proxy requires authentication")
                            : tr("This is not a SOCKS5 proxy"));
                    return;
                }
                saveProxy(wantedHost, port);
            },
            Qt::QueuedConnection);
    });
}

void I2pService::saveProxy(const QString& host, const int port)
{
    state_.proxyHost = host;
    state_.proxyPort = port;
    AppSettings::instance().setI2pProxy(state_.proxyHost.toStdString(), state_.proxyPort);
    client::setI2pSocksProxy(state_.proxyHost.toStdString(), state_.proxyPort);
    announce();
    inBackground([]() { client::restartI2pRouter(); });
    refresh();
}

void I2pService::setLoggingEnabled(bool on)
{
    if (state_.loggingEnabled == on) {
        return;
    }
    state_.loggingEnabled = on;
    bazarish::i2p::setI2pLogLevel(i2pLogLevelFor(on));
    AppSettings::instance().setI2pLogging(on);
    announce();
}

void I2pService::setPrivacyLevel(const int level)
{
    const int wanted = boundedPrivacyLevel(level);
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
        = client::GatewayAddress::fromLink(address.trimmed().toStdString());
    if (!parsed.has_value()) {
        emit events_.gatewayRefused(tr("This is not a gateway link"));
        return;
    }
    state_.gatewayChecking = true;
    announce();
    inBackground([this, parsed]() {
        const client::GatewayCheck check = client::checkGateway(parsed.value());
        QMetaObject::invokeMethod(
            this,
            [this, parsed, check]() {
                state_.gatewayChecking = false;
                if (!check.ok) {
                    bazarish::log::warn("i2p: gateway check: {}", check.error);
                    announce();
                    emit events_.gatewayRefused(check.wrongGateway
                            ? tr("The gateway at this address is not the one the link names")
                            : tr("Could not connect to this address"));
                    return;
                }
                AppSettings::instance().rememberGateway(parsed->toString());
                state_.gatewayAsked = true;
                state_.gatewayAddress = QString::fromStdString(parsed->link());
                state_.gatewayHost = QString::fromStdString(parsed->host);
                if (state_.gatewayEnabled) {
                    if (client::repointGatewayTransport(parsed.value())) {
                        transportAtStart_ = transport();
                        gatewayAtStart_ = state_.gatewayAddress;
                    }
                    noteChoiceInForce();
                } else {
                    announce();
                }
                emit events_.gatewaySaved();
            },
            Qt::QueuedConnection);
    });
}

QString I2pService::transport() const
{
    return transportOf(state_);
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
    const auto router = client::sharedI2pRouterIfRunning();
    const bool running = router.has_value();
    bool ready = false;
    const bool ownEngine = !state_.samEnabled && !state_.gatewayEnabled;
    int knownRouters = ownEngine ? static_cast<int>(client::knownRouterCount(i2pRoot())) : 0;
    int floodfills = 0;
    int inboundTunnels = 0;
    int outboundTunnels = 0;
    QStringList transports;
    QVariantList destinations;
    if (running) {
        ready = router->get().ready();
    }
    if (running && router->get().capabilities().routerCounters) {
        knownRouters = router->get().knownRouters();
        floodfills = router->get().floodfills();
        inboundTunnels = router->get().inboundTunnels();
        outboundTunnels = router->get().outboundTunnels();
        for (const bazarish::i2p::TransportPeer& peer : router->get().transportPeers()) {
            QString row = QString::fromStdString(peer.transport) + " · ";
            if (!peer.endpoint.empty()) {
                row += QString::fromStdString(peer.endpoint) + " · ";
            }
            row += QString::fromStdString(peer.ident);
            transports << row;
        }
        transports.sort();
    }
    if (running) {
        const bool many = manyAccounts();
        const bool detailKnown = canDetail(router);
        for (const bazarish::i2p::LocalDestination& dest : router->get().localDestinations()) {
            QVariantMap row;
            row[QStringLiteral("label")] = destinationLabel(dest, many);
            row[QStringLiteral("host")] = QString::fromStdString(dest.host);
            row[QStringLiteral("state")] = destinationState(dest);
            row[QStringLiteral("tunnelsIn")] = dest.inboundTunnels;
            row[QStringLiteral("tunnelsOut")] = dest.outboundTunnels;
            row[QStringLiteral("leaseSets")] = dest.remoteLeaseSets;
            row[QStringLiteral("localRoute")] = dest.localRoute;
            row[QStringLiteral("countsKnown")] = router->get().capabilities().destinationCounters;
            row[QStringLiteral("detailKnown")] = detailKnown;
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

void I2pService::inspectDestination(const QString& host)
{
    const auto router = client::sharedI2pRouterIfRunning();
    if (!canDetail(router)) {
        emit events_.destinationDetail(host, {});
        return;
    }
    std::optional<bazarish::i2p::DestinationDetail> detail;
    try {
        detail = router->get().destinationDetail(host.toStdString());
    } catch (const std::exception& error) {
        bazarish::log::warn("i2p: no detail for {}: {}", host.toStdString(), error.what());
        return;
    }
    emit events_.destinationDetail(
        host, detail.has_value() ? detailMap(detail.value(), refreshing_.value(host)) : QVariantMap());
}

void I2pService::refreshLeaseSet(const QString& localHost, const QString& remoteHost)
{
    const auto router = client::sharedI2pRouterIfRunning();
    if (!canDetail(router) || refreshing_.value(localHost).contains(remoteHost)) {
        return;
    }
    refreshing_[localHost].insert(remoteHost);
    inspectDestination(localHost);
    bazarish::i2p::Router& running = router->get();
    inBackground([this, &running, localHost, remoteHost]() {
        try {
            const bazarish::i2p::LeaseSetRefresh refresh
                = running.refreshLeaseSet(localHost.toStdString(), remoteHost.toStdString());
            const char* found = "none found";
            if (refresh.after.has_value()) {
                found = bazarish::i2p::publishedLater(*refresh.after, refresh.before)
                    ? "a newer one"
                    : "the one held";
            }
            bazarish::log::info(
                "i2p: lease set of {} looked up again: {}", remoteHost.toStdString(), found);
        } catch (const std::exception& error) {
            bazarish::log::warn(
                "i2p: lease set of {} not looked up: {}", remoteHost.toStdString(), error.what());
        }
        QMetaObject::invokeMethod(
            this,
            [this, localHost, remoteHost]() {
                const auto held = refreshing_.find(localHost);
                if (held != refreshing_.end() && held->remove(remoteHost) && held->isEmpty()) {
                    refreshing_.erase(held);
                }
                inspectDestination(localHost);
            },
            Qt::QueuedConnection);
    });
}

}  // namespace bazarish::app
