// Bazarish project (c) 2026
#include "CoreMain.hpp"

#include "AppSettings.hpp"
#include "CoreHost.hpp"
#include "CoreSocket.hpp"
#include "Notifier.hpp"
#include "PathText.hpp"
#include "SingleInstance.hpp"
#include "Translations.hpp"
#ifdef Q_OS_ANDROID
#include "AndroidShade.hpp"

#include <QtCore/private/qandroidextras_p.h>
#endif

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
#ifdef Q_OS_ANDROID
    // Qt holds the service's start until this kind of application exists.
    QAndroidService app(argc, argv);
#else
    QCoreApplication app(argc, argv);
#endif
    QCoreApplication::setApplicationName("Bazarish");
    QCoreApplication::setOrganizationName("Bazarish");
    const std::filesystem::path accounts = accountsRoot();
    std::filesystem::create_directories(accounts);
    SingleInstance instance(pathText(accounts));
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
#ifdef Q_OS_ANDROID
    AndroidShade shade(core, notifier, translations);
    QObject::connect(&core, &CoreHost::readyToExit, &shade, &AndroidShade::stop);
    AndroidShade::listening();
#endif
    QObject::connect(&core, &CoreHost::readyToExit, &app, [&listener]() {
        listener.flushWindow();
        exitNow();
    });
    return QCoreApplication::exec();
}

}  // namespace bazarish::app
