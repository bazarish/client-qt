// Bazarish project (c) 2026
#include "NetworkWatch.hpp"

#include <utility>

namespace bazarish::app {

namespace {

// Local and Site still reach a gateway on the same network.
bool gone(const QNetworkInformation::Reachability reachability)
{
    return reachability == QNetworkInformation::Reachability::Disconnected;
}

}  // namespace

NetworkWatch::NetworkWatch(std::function<void()> lost, std::function<void()> changed)
    : lost_(std::move(lost))
    , changed_(std::move(changed))
{
}

bool NetworkWatch::follow()
{
    // Qt answers the default with its dummy backend when no real one loads.
    if (!QNetworkInformation::loadBackendByFeatures(QNetworkInformation::Feature::Reachability)) {
        return false;
    }
    const QNetworkInformation* const information = QNetworkInformation::instance();
    reachability_ = information->reachability();
    medium_ = information->transportMedium();
    QObject::connect(information, &QNetworkInformation::reachabilityChanged, this,
        [this](const QNetworkInformation::Reachability now) { reachability(now); });
    QObject::connect(information, &QNetworkInformation::transportMediumChanged, this,
        [this](const QNetworkInformation::TransportMedium now) { medium(now); });
    if (gone(*reachability_)) {
        lost_();
    }
    return true;
}

void NetworkWatch::reachability(const QNetworkInformation::Reachability now)
{
    const bool moved = reachability_.has_value() && *reachability_ != now;
    reachability_ = now;
    if (!moved) {
        return;
    }
    if (gone(now)) {
        lost_();
    } else {
        changed_();
    }
}

void NetworkWatch::medium(const QNetworkInformation::TransportMedium now)
{
    const bool moved = medium_.has_value() && *medium_ != now;
    medium_ = now;
    if (moved && !(reachability_.has_value() && gone(*reachability_))) {
        changed_();
    }
}

}  // namespace bazarish::app
