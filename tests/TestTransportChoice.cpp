// Bazarish project (c) 2026
#include "AppSettings.hpp"
#include "I2pEvents.hpp"
#include "I2pRouter.hpp"
#include "I2pService.hpp"
#include "Wire.hpp"
#include "TestUtil.hpp"

#include <QCoreApplication>
#include <QElapsedTimer>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>

using namespace bazarish::app;
namespace fs = std::filesystem;

namespace {

constexpr int kSettleMs = 2000;
constexpr char kUnreachableGateway[]
    = "https://127.0.0.1:9/gw#aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
constexpr std::string_view kGatewayRefusal = "bazarish::i2p: the gateway would not open";

}  // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    const fs::path root = testutil::freshDir("transport-choice");
    fs::create_directories(root / "accounts");
    const std::string accountsDir = (root / "accounts").string();
#ifdef _WIN32
    CHECK(::_putenv_s("BAZARISH_ACCOUNTS_DIR", accountsDir.c_str()) == 0);
#else
    CHECK(::setenv("BAZARISH_ACCOUNTS_DIR", accountsDir.c_str(), 1) == 0);
#endif
    AppSettings::instance().rememberGateway(kUnreachableGateway);
    CHECK(!AppSettings::instance().gatewayEnabled());

    I2pService service;
    InProcessChannel channel;
    Relay core;
    core.attach(channel.first());
    service.attach(core);
    Relay window;
    window.attach(channel.second());
    I2pCommands commands;
    window.sendFrom(QStringLiteral("i2p"), commands);
    I2pEvents events;
    window.deliverTo(QStringLiteral("i2p"), events);
    bool gatewayOn = false;
    bool restartNeeded = false;
    QObject::connect(&events, &I2pEvents::state, [&](const I2pState& state) {
        gatewayOn = state.gatewayEnabled;
        restartNeeded = state.restartNeeded;
    });

    emit commands.useGateway(true);
    QElapsedTimer waited;
    waited.start();
    while (!gatewayOn && waited.elapsed() < kSettleMs) {
        QCoreApplication::processEvents();
    }
    CHECK(gatewayOn);
    CHECK(!restartNeeded);

    std::string refusal;
    try {
        bazarish::client::sharedI2pRouter(root / "router");
    } catch (const std::runtime_error& error) {
        refusal = error.what();
    }
    CHECK(refusal.starts_with(kGatewayRefusal));
    fs::remove_all(root);
    return 0;
}
