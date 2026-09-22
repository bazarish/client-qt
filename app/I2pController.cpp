// Bazarish project (c) 2026
#include "I2pController.hpp"

#include <QVariantMap>

#include "GatewayAddress.hpp"
#include "I2pRouter.hpp"
#include "AccountManager.hpp"
#include "AppSettings.hpp"

#include <bazarish/I2p.hpp>
#include <bazarish/Sam.hpp>

// Qt's "emit" keyword macro collides with bazarish::log::emit.
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
// The router serves the whole installation, so its state sits at the root of it
// rather than among the accounts.
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
    // libi2pd logging is off by default: fully silent.
    loggingEnabled_ = AppSettings::instance().i2pLogging();
    // Tunnel hop length. The default is the shortest tunnels: the application has
    // to be usable before it can be anything else, and the page states what one
    // hop does and does not conceal.
    privacyLevel_ = std::clamp(
        AppSettings::instance().i2pTunnelLength(), kMinimalPrivacyLevel, kMaxPrivacyLevel);
    // The clearnet side of the router: nothing by default, so it goes straight
    // out. Read before the router is brought up, because the transports read it
    // as they start.
    {
        proxyHost_ = QString::fromStdString(AppSettings::instance().i2pProxyHost());
        proxyPort_ = AppSettings::instance().i2pProxyPort();
        client::setI2pSocksProxy(proxyHost_.toStdString(), proxyPort_);
        if (!proxyHost_.isEmpty()) {
            bazarish::log::info("i2p: clearnet side goes through socks://{}:{}",
                proxyHost_.toStdString(), proxyPort_);
        }
    }
    // Which transport carries this process. The engine inside it unless the user
    // has said otherwise: a router outside is somebody else's process, and
    // putting the traffic through it is a decision to make deliberately.
    samHost_ = QString::fromStdString(AppSettings::instance().samHost());
    samPort_ = AppSettings::instance().samPort();
    samEnabled_ = AppSettings::instance().samEnabled();
    gatewayAsked_ = AppSettings::instance().gatewayAsked();
    gatewayEnabled_ = AppSettings::instance().gatewayEnabled();
    gatewayAddress_ = QString::fromStdString(AppSettings::instance().gatewayAddress());
    if (samEnabled_) {
        client::setSamTransport(samHost_.toStdString(), samPort_);
    }
    // The router is the transport, not a feature: without it there is no way to
    // reach a server, so there is nothing to turn off.
    client::setI2pEnabled(true);
    bazarish::i2p::setI2pLogging(loggingEnabled_);
    client::setTunnelPrivacy(privacyForLevel(privacyLevel_));
    // Brought up at launch so it passively learns the network (routers +
    // floodfills) even before any account uses it.
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

void I2pController::saveSam(const bool enabled, const QString& host, const int port)
{
    samEnabled_ = enabled;
    samHost_ = host.trimmed();
    samPort_ = port;
    AppSettings::instance().setSam(samEnabled_, samHost_.toStdString(), samPort_);
    // Deliberately nothing else: this process is already running one engine or
    // the other, and libi2pd cannot be initialised a second time in it.
    emit samChanged();
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
        // Heavyweight (the engine's threads stop and start), so off the GUI thread.
        const std::filesystem::path dataDir = i2pRoot();
        std::thread([dataDir]() { client::restartI2pRouter(dataDir); }).detach();
    }
    refresh();
}

void I2pController::reconcileRouter()
{
    const std::filesystem::path dir = i2pRoot();
    // Starting or stopping the engine is heavyweight (it joins worker threads), so
    // do it off the GUI thread. reconcileI2pRouter reads the enable flag itself, so
    // rapid toggles converge on the final state (idempotent, mutex-guarded).
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
    // The check talks to a host over the network and may wait out a timeout; the
    // interface is not the thread to do that on.
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
                // Stored with the key the host presented, which every later
                // connection is checked against.
                AppSettings::instance().setGateway(parsed->toString(), check.pin);
                gatewayAsked_ = true;
                gatewayEnabled_ = true;
                gatewayAddress_ = QString::fromStdString(parsed->toString());
                emit gatewayChanged();
                emit gatewaySaved();
            },
            Qt::QueuedConnection);
    }).detach();
}

void I2pController::skipGateway()
{
    AppSettings::instance().skipGateway();
    gatewayAsked_ = true;
    gatewayEnabled_ = false;
    gatewayAddress_.clear();
    emit gatewayChanged();
}

void I2pController::clearGateway()
{
    AppSettings::instance().skipGateway();
    gatewayEnabled_ = false;
    gatewayAddress_.clear();
    emit gatewayChanged();
}

void I2pController::refresh()
{
    // Never start the router here: report it only when another path (warmup, a
    // connect, a call) has already brought it up.
    bazarish::i2p::Router* const router = client::sharedI2pRouterIfRunning();
    const bool running = router != nullptr;
    bool ready = false;
    // A stopped router still has a netDb on disk, and how big it is says whether
    // it is waiting for a bootstrap or just about to come up.
    int knownRouters = samEnabled_ ? 0 : static_cast<int>(client::knownRouterCount(i2pRoot()));
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
    // A router outside this process answers nothing about the network it is on,
    // and asking throws rather than returning a zero that would read like a fact.
    if (running && router->capabilities().routerCounters) {
        knownRouters = router->knownRouters();
        floodfills = router->floodfills();
        inboundTunnels = router->inboundTunnels();
        outboundTunnels = router->outboundTunnels();
        for (const bazarish::i2p::TransportPeer& peer : router->transportPeers()) {
            // Only the exception is marked. This router relays no transit and is no
            // floodfill, so nothing has a reason to dial it and every session is
            // one it opened: labelling them all "outgoing" said nothing.
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
        // The destinations are this process's own bookkeeping, so both transports
        // can name them - only their tunnel counts belong to the engine.
        // One router serves every open account, so a destination says whose it is
        // as soon as there is more than one account to confuse it with.
        const bool manyAccounts = client::AccountManager(accountsRoot()).list().size() > 1;
        for (const bazarish::i2p::LocalDestination& dest : router->localDestinations()) {
            QVariantMap row;
            const QString what = dest.label.empty() ? QStringLiteral("Destination")
                                                    : QString::fromStdString(dest.label);
            row[QStringLiteral("label")] = (manyAccounts && !dest.owner.empty())
                ? (QString::fromStdString(dest.owner) + QStringLiteral(": ") + what)
                : what;
            row[QStringLiteral("host")] = QString::fromStdString(dest.host);
            // A destination on its way out has no tunnels either, and calling that
            // "building" said the opposite of what was happening.
            row[QStringLiteral("state")] = dest.closing ? QStringLiteral("closing")
                : dest.ready
                ? (dest.published ? QStringLiteral("published") : QStringLiteral("ready"))
                : QStringLiteral("building");
            // Zero here means "not known" under an external router, which is
            // what the counters capability is for.
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
