// Bazarish project (c) 2026
#include "I2pController.hpp"

#include <QVariantMap>

#include "I2pRouter.hpp"
#include "AccountManager.hpp"

#include <bazarish/I2p.hpp>

// Qt's "emit" keyword macro collides with bazarish::log::emit.
#pragma push_macro("emit")
#undef emit
#include <bazarish/Log.hpp>
#pragma pop_macro("emit")

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

namespace bazarish::app {

namespace {
std::filesystem::path accountsRoot()
{
    if (const char* const env = std::getenv("BAZARISH_ACCOUNTS_DIR");
        env != nullptr && env[0] != '\0') {
        return std::filesystem::path(env);
    }
    return client::AccountManager::defaultRoot();
}

// The router serves the whole installation, so its state sits beside the
// accounts directory rather than among the accounts themselves.
std::filesystem::path i2pRoot()
{
    return accountsRoot().parent_path() / "i2p";
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
    // libi2pd logging is off unless a "1" was persisted (default fully silent).
    {
        std::ifstream in(loggingPath());
        std::string value;
        std::getline(in, value);
        loggingEnabled_ = value == "1";
    }
    // Tunnel hop length. Absent => the shortest tunnels: the app has to be usable
    // before it can be anything else, and the page says plainly what one hop does
    // and does not hide.
    {
        std::ifstream in(privacyPath());
        std::string value;
        std::getline(in, value);
        const int level = value.empty() ? kMinimalPrivacyLevel : std::atoi(value.c_str());
        privacyLevel_ = std::clamp(level, kMinimalPrivacyLevel, kMaxPrivacyLevel);
    }
    // The clearnet side of the router: nothing by default, so it goes straight
    // out. Read before the router is brought up, because the transports read it
    // as they start.
    {
        std::ifstream in(proxyPath());
        std::string value;
        std::getline(in, value);
        const std::size_t colon = value.rfind(':');
        if (colon != std::string::npos) {
            proxyHost_ = QString::fromStdString(value.substr(0, colon));
            proxyPort_ = std::atoi(value.c_str() + colon + 1);
        }
        if (proxyHost_.isEmpty() || proxyPort_ <= 0) {
            proxyHost_.clear();
            proxyPort_ = 0;
        }
        client::setI2pSocksProxy(proxyHost_.toStdString(), proxyPort_);
        if (!proxyHost_.isEmpty()) {
            bazarish::log::info("i2p: clearnet side goes through socks://{}:{}",
                proxyHost_.toStdString(), proxyPort_);
        }
    }
    // The embedded router is the transport, not a feature: without it there is
    // no way to reach a server, so there is nothing to turn off.
    client::setI2pEnabled(true);
    bazarish::i2p::setI2pLogging(loggingEnabled_);
    client::setTunnelPrivacy(privacyForLevel(privacyLevel_));
    // Brought up at launch so it passively learns the network (routers +
    // floodfills) even before any account uses it.
    reconcileRouter();
    refresh();
}

std::filesystem::path I2pController::loggingPath() const
{
    return accountsRoot() / ".i2p-logging";
}

std::filesystem::path I2pController::proxyPath() const
{
    return accountsRoot() / ".i2p-proxy";
}

void I2pController::saveProxy(const QString& host, const int port, const bool restartNow)
{
    const QString wantedHost = host.trimmed();
    const bool clearing = wantedHost.isEmpty() || port <= 0;
    proxyHost_ = clearing ? QString() : wantedHost;
    proxyPort_ = clearing ? 0 : port;
    {
        std::ofstream out(proxyPath(), std::ios::trunc);
        if (!clearing) {
            out << proxyHost_.toStdString() << ":" << proxyPort_;
        }
    }
    client::setI2pSocksProxy(proxyHost_.toStdString(), proxyPort_);
    emit proxyChanged();
    if (restartNow) {
        // Heavyweight (the engine's threads stop and start), so off the GUI thread.
        const std::filesystem::path dataDir = i2pRoot();
        std::thread([dataDir]() { client::restartI2pRouter(dataDir); }).detach();
    }
    refresh();
}

std::filesystem::path I2pController::privacyPath() const
{
    return accountsRoot() / ".i2p-privacy";
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
    std::ofstream out(loggingPath(), std::ios::trunc);
    out << (on ? "1" : "0");
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
    std::ofstream out(privacyPath(), std::ios::trunc);
    out << wanted;
    emit privacyLevelChanged();
}

int I2pController::minKnownRouters() const
{
    return static_cast<int>(client::kMinKnownRouters);
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
    int knownRouters = static_cast<int>(client::knownRouterCount(i2pRoot()));
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
            row[QStringLiteral("tunnelsIn")] = dest.inboundTunnels;
            row[QStringLiteral("tunnelsOut")] = dest.outboundTunnels;
            row[QStringLiteral("leaseSets")] = dest.remoteLeaseSets;
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
