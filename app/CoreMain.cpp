// Bazarish project (c) 2026
#include "CoreMain.hpp"

#include "AppSettings.hpp"
#include "CoreHost.hpp"
#include "CoreSocket.hpp"
#include "Notifier.hpp"
#include "SingleInstance.hpp"
#include "Translations.hpp"

#pragma push_macro("emit")
#undef emit
#include <bazarish/Log.hpp>
#pragma pop_macro("emit")

#include <QCoreApplication>

#include <cstdio>
#include <cstdlib>
#include <filesystem>

namespace bazarish::app {

namespace {

void exitNow()
{
    std::fflush(nullptr);
    std::_Exit(EXIT_SUCCESS);
}

}  // namespace

int runCore(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName("Bazarish");
    QCoreApplication::setOrganizationName("Bazarish");
    const std::filesystem::path accounts = accountsRoot();
    std::filesystem::create_directories(accounts);
    SingleInstance instance(QString::fromStdString(accounts.string()));
    if (!instance.claim()) {
        bazarish::log::info("another Bazarish core already has this account folder");
        return EXIT_FAILURE;
    }
    Translations translations;
    QCoreApplication::installTranslator(&translations);
    CoreHost core;
    QObject::connect(
        &core, &CoreHost::languageChosen, &translations, &Translations::setLanguage);
    core.start();
    Notifier notifier(core);
    CoreListener listener(core, coreSocketName());
    QObject::connect(&core, &CoreHost::windowBroken, &listener, [&listener](const QString& reason) {
        bazarish::log::warn("wire: {}; letting the window go", reason.toStdString());
        listener.dropWindow();
    });
    QObject::connect(&core, &CoreHost::readyToExit, &app, exitNow);
    return QCoreApplication::exec();
}

}  // namespace bazarish::app
