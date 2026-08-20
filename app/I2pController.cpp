// Bazarish project (c) 2026
#include "I2pController.hpp"

#include <QVariantMap>

#include "I2pRouter.hpp"
#include "ProfileManager.hpp"

#include <bazarish/I2p.hpp>

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

namespace bazarish::app {

namespace {
std::filesystem::path profilesRoot()
{
    if (const char* const env = std::getenv("BAZARISH_PROFILES_DIR");
        env != nullptr && env[0] != '\0') {
        return std::filesystem::path(env);
    }
    return client::ProfileManager::defaultRoot();
}

// The router serves the whole installation, so its state sits beside the
// profiles directory rather than among the profiles themselves.
std::filesystem::path i2pRoot()
{
    return profilesRoot().parent_path() / "i2p";
}

bazarish::i2p::Privacy privacyToProfile(const int level)
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
    // The setting persists across runs: an "0" in the file means I2P was turned
    // off and must stay off (never re-enabled automatically). Absent => on.
    {
        std::ifstream in(settingPath());
        std::string value;
        std::getline(in, value);
        enabled_ = value != "0";
    }
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
    client::setI2pEnabled(enabled_);
    bazarish::i2p::setI2pLogging(loggingEnabled_);
    client::setTunnelPrivacy(privacyToProfile(privacyLevel_));
    // When enabled, bring the embedded router up at launch so it passively learns
    // the network (routers + floodfills) even before any session uses it; when
    // disabled it stays down.
    reconcileRouter();
    refresh();
}

std::filesystem::path I2pController::settingPath() const
{
    return profilesRoot() / ".i2p-enabled";
}

std::filesystem::path I2pController::loggingPath() const
{
    return profilesRoot() / ".i2p-logging";
}

std::filesystem::path I2pController::privacyPath() const
{
    return profilesRoot() / ".i2p-privacy";
}

void I2pController::reconcileRouter()
{
    const std::filesystem::path dir = i2pRoot();
    // Starting or stopping the engine is heavyweight (it joins worker threads), so
    // do it off the GUI thread. reconcileI2pRouter reads the enable flag itself, so
    // rapid toggles converge on the final state (idempotent, mutex-guarded).
    std::thread([dir]() { client::reconcileI2pRouter(dir); }).detach();
}

void I2pController::setEnabled(bool on)
{
    if (enabled_ == on) {
        return;
    }
    enabled_ = on;
    client::setI2pEnabled(on);
    std::ofstream out(settingPath(), std::ios::trunc);
    out << (on ? "1" : "0");
    emit enabledChanged();
    // Honestly start or stop the embedded router to match the toggle.
    reconcileRouter();
    refresh();
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
    client::setTunnelPrivacy(privacyToProfile(wanted));
    std::ofstream out(privacyPath(), std::ios::trunc);
    out << wanted;
    emit privacyLevelChanged();
}

void I2pController::refresh()
{
    // Never start the router here: report it only when another path (warmup, a
    // connect, a call) has already brought it up.
    bazarish::i2p::Router* const router = client::sharedI2pRouterIfRunning();
    const bool running = router != nullptr;
    bool ready = false;
    int knownRouters = 0;
    int floodfills = 0;
    int inboundTunnels = 0;
    int outboundTunnels = 0;
    QStringList transports;
    QVariantList destinations;
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
        // One router serves every open profile, so a destination says whose it is
        // as soon as there is more than one profile to confuse it with.
        const bool manyProfiles = client::ProfileManager(profilesRoot()).list().size() > 1;
        for (const bazarish::i2p::LocalDestination& dest : router->localDestinations()) {
            QVariantMap row;
            const QString what = dest.label.empty() ? QStringLiteral("Destination")
                                                    : QString::fromStdString(dest.label);
            row[QStringLiteral("label")] = (manyProfiles && !dest.owner.empty())
                ? (QString::fromStdString(dest.owner) + QStringLiteral(": ") + what)
                : what;
            row[QStringLiteral("host")] = QString::fromStdString(dest.host);
            row[QStringLiteral("state")] = dest.ready
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
        && destinations == destinations_) {
        return;
    }
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
