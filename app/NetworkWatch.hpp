// Bazarish project (c) 2026
#pragma once

#include <QNetworkInformation>
#include <QObject>

#include <functional>
#include <optional>

namespace bazarish::app {

// Turns the platform's word that the network changed into one call of changed.
class NetworkWatch : public QObject {
public:
    explicit NetworkWatch(std::function<void()> changed);

    // False when no backend loads: nothing then tells this process of a new network.
    bool follow();

    void reachability(QNetworkInformation::Reachability now);
    void medium(QNetworkInformation::TransportMedium now);

private:
    const std::function<void()> changed_;
    std::optional<QNetworkInformation::Reachability> reachability_;
    std::optional<QNetworkInformation::TransportMedium> medium_;
};

}  // namespace bazarish::app
