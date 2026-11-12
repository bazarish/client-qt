// Bazarish project (c) 2026
#include "NetworkWatch.hpp"

#include <utility>

namespace bazarish::app {

NetworkWatch::NetworkWatch(std::function<void()> changed)
    : changed_(std::move(changed))
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
    return true;
}

void NetworkWatch::reachability(const QNetworkInformation::Reachability now)
{
    const bool moved = reachability_.has_value() && *reachability_ != now;
    reachability_ = now;
    if (moved) {
        changed_();
    }
}

void NetworkWatch::medium(const QNetworkInformation::TransportMedium now)
{
    const bool moved = medium_.has_value() && *medium_ != now;
    medium_ = now;
    if (moved) {
        changed_();
    }
}

}  // namespace bazarish::app
