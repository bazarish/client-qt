// Bazarish project (c) 2026
#include "I2pController.hpp"

#include "Wire.hpp"

#include <QVariantMap>

#include <algorithm>
#include <tuple>

namespace bazarish::app {

namespace {

const QString kI2pScope = QStringLiteral("i2p");

auto statusOf(const I2pState& state)
{
    return std::tie(state.running, state.ready, state.knownRouters, state.minKnownRouters,
        state.floodfills, state.inboundTunnels, state.outboundTunnels, state.transports,
        state.destinations, state.proxyNtcp2);
}

auto samOf(const I2pState& state)
{
    return std::tie(state.samEnabled, state.samChecking, state.samHost, state.samPort);
}

auto gatewayOf(const I2pState& state)
{
    return std::tie(state.gatewayAsked, state.gatewayEnabled, state.gatewayChecking,
        state.gatewayAddress, state.gatewayHost);
}

auto transportOf(const I2pState& state)
{
    return std::tie(state.samEnabled, state.gatewayEnabled, state.restartNeeded);
}

}  // namespace

I2pController::I2pController(QObject* parent)
    : QObject(parent)
{
    connect(&events_, &I2pEvents::state, this, &I2pController::take);
    connect(&events_, &I2pEvents::proxyRefused, this, &I2pController::proxyRefused);
    connect(&events_, &I2pEvents::gatewaySaved, this, &I2pController::gatewaySaved);
    connect(&events_, &I2pEvents::gatewayRefused, this, &I2pController::gatewayRefused);
    connect(&events_, &I2pEvents::samSaved, this, &I2pController::samSaved);
    connect(&events_, &I2pEvents::samRefused, this, &I2pController::samRefused);
}

void I2pController::attach(Relay& relay)
{
    relay.sendFrom(kI2pScope, commands_);
    relay.deliverTo(kI2pScope, events_);
}

void I2pController::take(const I2pState& state)
{
    const I2pState was = std::exchange(state_, state);
    if (was.loggingEnabled != state_.loggingEnabled) {
        emit loggingChanged();
    }
    if (was.privacyLevel != state_.privacyLevel) {
        emit privacyLevelChanged();
    }
    if (statusOf(was) != statusOf(state_)) {
        emit statusChanged();
    }
    if (was.proxyHost != state_.proxyHost || was.proxyPort != state_.proxyPort) {
        emit proxyChanged();
    }
    if (was.proxyChecking != state_.proxyChecking) {
        emit proxyCheckingChanged();
    }
    if (samOf(was) != samOf(state_)) {
        emit samChanged();
    }
    if (gatewayOf(was) != gatewayOf(state_)) {
        emit gatewayChanged();
    }
    if (transportOf(was) != transportOf(state_)) {
        emit transportChanged();
    }
}

void I2pController::setPrivacyLevel(const int level)
{
    const int wanted = std::clamp(level, kMinimalPrivacyLevel, kMaxPrivacyLevel);
    if (state_.privacyLevel == wanted) {
        return;
    }
    state_.privacyLevel = wanted;
    emit privacyLevelChanged();
    emit commands_.setPrivacyLevel(wanted);
}

void I2pController::setLoggingEnabled(const bool on)
{
    if (state_.loggingEnabled == on) {
        return;
    }
    state_.loggingEnabled = on;
    emit loggingChanged();
    emit commands_.setLoggingEnabled(on);
}

bool I2pController::tunnelsBuilding() const
{
    if (!state_.running) {
        return true;
    }
    const bool building = std::any_of(state_.destinations.cbegin(), state_.destinations.cend(),
        [](const QVariant& entry) {
            return entry.toMap().value(QStringLiteral("state")).toString()
                == QLatin1String("building");
        });
    return building || state_.destinations.isEmpty();
}

QString I2pController::transport() const
{
    if (state_.gatewayEnabled) {
        return QStringLiteral("gateway");
    }
    return state_.samEnabled ? QStringLiteral("sam") : QStringLiteral("embedded");
}

void I2pController::checkAndSaveProxy(const QString& host, const int port)
{
    emit commands_.checkAndSaveProxy(host, port);
}

void I2pController::checkAndSaveSam(const QString& host, const int port)
{
    emit commands_.checkAndSaveSam(host, port);
}

void I2pController::useSam(const bool on)
{
    emit commands_.useSam(on);
}

void I2pController::useGateway(const bool on)
{
    emit commands_.useGateway(on);
}

void I2pController::checkAndSaveGateway(const QString& address)
{
    emit commands_.checkAndSaveGateway(address);
}

void I2pController::skipGateway()
{
    emit commands_.skipGateway();
}

void I2pController::clearGateway()
{
    emit commands_.clearGateway();
}

void I2pController::useEmbedded()
{
    emit commands_.useEmbedded();
}

void I2pController::refresh()
{
    emit commands_.refresh();
}

}  // namespace bazarish::app
