// Bazarish project (c) 2026
#include "AppController.hpp"
#include "AppFont.hpp"
#include "CoreHost.hpp"
#include "Wire.hpp"
#include "AvatarStore.hpp"
#include "PictureStore.hpp"
#include "I2pController.hpp"
#include "DesktopEntry.hpp"
#ifdef Q_OS_WIN
#include "UrlScheme.hpp"
#endif
#include "Identicon.hpp"
#include "PathText.hpp"
#include "SingleInstance.hpp"
#include "Translations.hpp"
#include "AudioRoute.hpp"
#include "Notifier.hpp"
#include "TrayIcon.hpp"
#ifdef Q_OS_ANDROID
#include "AndroidShade.hpp"
#include "AndroidWindow.hpp"
#include "CoreMain.hpp"
#include "CoreSocket.hpp"
#endif

#pragma push_macro("emit")
#undef emit
#ifdef _WIN32
// Qt uses std::min and std::max, which the unguarded header takes for itself.
#define NOMINMAX
#include <windows.h>
#endif

#include <bazarish/Links.hpp>
#include <bazarish/Log.hpp>
#include <bazarish/ServerDescriptor.hpp>
#pragma pop_macro("emit")

#include <QColor>
#include <QDir>
#include <QFile>
#include <QApplication>
#include <QFileOpenEvent>
#include <QEvent>
#include <QGuiApplication>
#include <QIcon>
#include <QImage>
#include <QLibraryInfo>
#include <QPalette>
#include <QPixmap>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QInputMethod>
#include <QWindow>
#include <QUrl>

#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>
#include <utility>

#ifdef _WIN32

namespace {

constexpr const char* kConsoleFlag = "--console";


void attachConsole()
{
    if (::AttachConsole(ATTACH_PARENT_PROCESS) == 0 && ::AllocConsole() == 0) {
        return;
    }
    if (std::freopen("CONOUT$", "w", stdout) == nullptr
        || std::freopen("CONOUT$", "w", stderr) == nullptr) {
        ::MessageBoxA(nullptr, "Could not write to the console.", "Bazarish", MB_ICONWARNING);
    }
}

}  // namespace

#endif

#ifdef Q_OS_ANDROID
namespace {

// Qt's SurfaceView: the TextureView it picked for the window re-drew every frame in Java.
constexpr const char* kSurfaceViewContainer = "0";
// Qt draws no text handles or menu and selects no word: the app's own selection menu does.
constexpr const char* kNoQtTextHandles = "1";

}  // namespace
#endif

namespace {
// An input method holds the word being typed out of the field until it is committed.
class PressCommitsWord : public QObject {
protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        const QEvent::Type type = event->type();
        if ((type == QEvent::MouseButtonPress || type == QEvent::TouchBegin)
            && qobject_cast<QWindow*>(watched) != nullptr) {
            QGuiApplication::inputMethod()->commit();
        }
        return QObject::eventFilter(watched, event);
    }
};

// macOS hands a bazarish:// link to the running application as an event, not as an argument.
class LinkCatcher : public QObject {
public:
    void handOverTo(bazarish::app::AppController& controller)
    {
        controller_ = &controller;
        if (waiting_.isEmpty()) {
            return;
        }
        const QString link = waiting_;
        waiting_.clear();
        controller_->openLink(link);
    }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (event->type() != QEvent::FileOpen) {
            return QObject::eventFilter(watched, event);
        }
        const QString link = static_cast<QFileOpenEvent*>(event)->url().toString();
        if (controller_ == nullptr) {
            waiting_ = link;
            return true;
        }
        controller_->openLink(link);
        return true;
    }

private:
    bazarish::app::AppController* controller_ = nullptr;
    QString waiting_;
};

void exitNow()
{
    std::fflush(nullptr);
    std::_Exit(EXIT_SUCCESS);
}

constexpr const char* kBrandCanvas = "#16191c";
constexpr const char* kBrandSurface = "#1b2026";
constexpr const char* kBrandSurfaceAlt = "#232a31";
constexpr const char* kBrandText = "#d7dbd8";
constexpr const char* kBrandTextDim = "#8b948c";
constexpr const char* kBrandNeon = "#39ff14";
constexpr const char* kBrandAccentInk = "#11151a";

#ifdef Q_OS_LINUX
#ifndef BAZARISH_STATIC_QT
bool platformThemeIsHere(const QString& key)
{
    const QDir themes(QLibraryInfo::path(QLibraryInfo::PluginsPath)
        + QStringLiteral("/platformthemes"));
    for (const QString& file : themes.entryList({QStringLiteral("*.so")}, QDir::Files)) {
        if (file.contains(key)) {
            return true;
        }
    }
    return false;
}
#endif

// Qt draws a file chooser of its own unless a platform theme hands it the desktop's.
void useTheDesktopsDialogs()
{
#ifdef BAZARISH_STATIC_QT
    // The only theme linked in: any other name the desktop sets falls back to Qt's own chooser.
    qputenv("QT_QPA_PLATFORMTHEME", "xdgdesktopportal");
#else
    const QString asked = qEnvironmentVariable("QT_QPA_PLATFORMTHEME");
    if (!asked.isEmpty() && platformThemeIsHere(asked)) {
        return;
    }
    for (const QString& candidate :
        {QStringLiteral("xdgdesktopportal"), QStringLiteral("gtk3")}) {
        if (platformThemeIsHere(candidate)) {
            qputenv("QT_QPA_PLATFORMTHEME", candidate.toLatin1());
            return;
        }
    }
#endif
}
#endif

}  // namespace

int main(int argc, char** argv)
{
#ifdef Q_OS_ANDROID
    bazarish::app::AndroidShade::routeStderrToLogcat();
    // The service and the activity are started with these by the manifest.
    if (argc > 1 && std::strcmp(argv[argc - 1], "--core") == 0) {
        return bazarish::app::runCore(argc, argv);
    }
    qputenv("QT_ANDROID_SURFACE_CONTAINER_TYPE", kSurfaceViewContainer);
    qputenv("QT_QPA_NO_TEXT_HANDLES", kNoQtTextHandles);
#endif
#ifdef Q_OS_LINUX
    useTheDesktopsDialogs();
#endif

#ifdef _WIN32
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], kConsoleFlag) == 0) {
            attachConsole();
            break;
        }
    }
#endif

#ifdef _WIN32
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
        qputenv("QT_QPA_PLATFORM", "windows:fontengine=freetype");
    }
#endif
#ifdef Q_OS_MACOS
    // Under CoreText the COLR emoji font draws different emoji alike.
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
        qputenv("QT_QPA_PLATFORM", "cocoa:fontengine=freetype");
    }
#endif

    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORMTHEME")) {
        const QString themes
            = QLibraryInfo::path(QLibraryInfo::PluginsPath) + QStringLiteral("/platformthemes/");
        const std::array<std::pair<const char*, const char*>, 2> candidates{{
            {"libqxdgdesktopportal.so", "xdgdesktopportal"},
            {"libqgtk3.so", "gtk3"},
        }};
        for (const auto& [plugin, name] : candidates) {
            if (QFile::exists(themes + QLatin1String(plugin))) {
                qputenv("QT_QPA_PLATFORMTHEME", name);
                break;
            }
        }
    }

#ifdef BAZARISH_STATIC_QT
    // A static musl binary cannot dlopen a graphics driver; left to choose, Qt draws nothing.
    qputenv("QT_QUICK_BACKEND", "software");
#endif
    QApplication app(argc, argv);
    LinkCatcher links;
    app.installEventFilter(&links);
    PressCommitsWord words;
    app.installEventFilter(&words);
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--allow-facade-without-i2p-for-dev-purposes") == 0) {
            bazarish::setAllowFacadeWithoutI2pForDevPurposes(true);
            bazarish::log::warn("talking to facades without I2P: every request leaves this"
                                " machine in the clear. Development only.");
        }
    }
    QGuiApplication::setApplicationName("Bazarish");
    QGuiApplication::setOrganizationName("Bazarish");

    QQuickStyle::setStyle("Basic");

    bazarish::app::installAppFont();

    constexpr std::array<int, 7> kIconSizes{16, 22, 24, 32, 48, 64, 128};
    const QImage iconMaster(":/icon/bazarish.png");
    QIcon appIcon;
    for (const int size : kIconSizes) {
        appIcon.addPixmap(QPixmap::fromImage(
            iconMaster.scaled(size, size, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
    }
    QGuiApplication::setWindowIcon(appIcon);
    QGuiApplication::setDesktopFileName("bazarish");
#ifndef Q_OS_ANDROID
    try {
        bazarish::app::ensureDesktopEntry();
#ifdef Q_OS_WIN
        bazarish::app::ensureUrlScheme();
#endif
    } catch (const std::exception& error) {
        bazarish::log::warn("desktop entry not installed: {}", error.what());
    }
#endif

    QPalette palette;
    palette.setColor(QPalette::Window, QColor(kBrandCanvas));
    palette.setColor(QPalette::WindowText, QColor(kBrandText));
    palette.setColor(QPalette::Base, QColor(kBrandSurface));
    palette.setColor(QPalette::AlternateBase, QColor(kBrandSurfaceAlt));
    palette.setColor(QPalette::Text, QColor(kBrandText));
    palette.setColor(QPalette::PlaceholderText, QColor(kBrandTextDim));
    palette.setColor(QPalette::Button, QColor(kBrandSurface));
    palette.setColor(QPalette::ButtonText, QColor(kBrandText));
    palette.setColor(QPalette::Highlight, QColor(kBrandNeon));
    palette.setColor(QPalette::HighlightedText, QColor(kBrandAccentInk));
    palette.setColor(QPalette::ToolTipBase, QColor(kBrandSurface));
    palette.setColor(QPalette::ToolTipText, QColor(kBrandText));
    QGuiApplication::setPalette(palette);

    QString link;
    const QString scheme = QString::fromLatin1(bazarish::kUriScheme) + QStringLiteral("://");
    for (int i = 1; i < argc; ++i) {
        const QString argument = QString::fromLocal8Bit(argv[i]);
        if (argument.startsWith(scheme)) {
            link = argument;
            break;
        }
    }

    const std::filesystem::path accounts = bazarish::app::AppController::accountsFolder();
    std::filesystem::create_directories(accounts);
    bazarish::app::SingleInstance instance(bazarish::app::pathText(accounts));
    // On Android the core's own process holds the folder.
#ifndef Q_OS_ANDROID
    if (!instance.claim()) {
        const bool handed = instance.handOver(link);
        bazarish::log::info("another Bazarish already has this account folder; {}",
            handed ? "brought its window forward" : "it is not answering");
        return handed ? 0 : 1;
    }
#endif

    QQmlApplicationEngine engine;
    bazarish::app::Translations translations;
    QCoreApplication::installTranslator(&translations);
    QObject::connect(&translations, &bazarish::app::Translations::languageChanged, &engine,
        &QQmlEngine::retranslate);
    engine.rootContext()->setContextProperty("Tr", &translations);
    engine.addImageProvider("identicon", new bazarish::app::IdenticonProvider());
    engine.addImageProvider("avatar", new bazarish::app::AvatarProvider());
    engine.addImageProvider("picture", new bazarish::app::PictureProvider());
    engine.addImageProvider("qr", new bazarish::app::QrImageProvider());

    const auto wireBroken = [](const QString& reason) {
        bazarish::log::error("wire: {}", reason.toStdString());
        QCoreApplication::exit(1);
    };
#ifdef Q_OS_ANDROID
    bazarish::app::Relay windowRelay;
    QObject::connect(&windowRelay, &bazarish::app::Relay::broken, &app, wireBroken);
    bazarish::app::AppController controller(translations);
    controller.attach(windowRelay);
    bazarish::app::I2pController i2pController;
    i2pController.attach(windowRelay);
    bazarish::app::CoreLink coreLink(windowRelay, bazarish::app::coreSocketName());
    bazarish::app::AndroidWindow androidWindow(controller);
    QObject::connect(&androidWindow, &bazarish::app::AndroidWindow::coreListening, &coreLink,
        &bazarish::app::CoreLink::open);
    QObject::connect(&coreLink, &bazarish::app::CoreLink::lost, &app, [&controller]() {
        if (controller.coreClosing()) {
            exitNow();
        }
        controller.detach();
        bazarish::app::AndroidWindow::startCore();
    });
    bazarish::app::AndroidWindow::startCore();
#else
    bazarish::app::CoreHost core;
    QObject::connect(&core, &bazarish::app::CoreHost::windowBroken, &app, wireBroken);
    core.start();
    bazarish::app::InProcessChannel channel;
    bazarish::app::Relay windowRelay;
    QObject::connect(&windowRelay, &bazarish::app::Relay::broken, &app, wireBroken);
    windowRelay.attach(channel.second());
    bazarish::app::AppController controller(translations);
    controller.attach(windowRelay);
    bazarish::app::I2pController i2pController;
    i2pController.attach(windowRelay);
    core.attachWindow(channel.first());
#endif
    engine.rootContext()->setContextProperty("App", &controller);
    engine.rootContext()->setContextProperty("Avatars", &bazarish::app::AvatarStore::instance());
    engine.rootContext()->setContextProperty("Audio", &bazarish::app::AudioRoute::instance());
    engine.rootContext()->setContextProperty(
        "Pictures", &bazarish::app::PictureStore::instance());

    engine.rootContext()->setContextProperty("I2p", &i2pController);
    QObject::connect(&translations, &bazarish::app::Translations::languageChanged, &controller,
        &bazarish::app::AppController::retranslate);

    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed, &app,
        []() { QCoreApplication::exit(-1); }, Qt::QueuedConnection);

    engine.load(QUrl(QStringLiteral("qrc:/qt/qml/Bazarish/app/qml/Main.qml")));
    if (engine.rootObjects().isEmpty()) {
        return -1;
    }

    const auto raiseWindow = [&engine]() {
        if (engine.rootObjects().isEmpty()) {
            return;
        }
        auto* const window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        if (window == nullptr) {
            return;
        }
        window->setWindowStates(window->windowStates() & ~Qt::WindowMinimized);
        window->show();
        window->raise();
        window->requestActivate();
    };
    QObject::connect(&instance, &bazarish::app::SingleInstance::showRequested, &app, raiseWindow);
    QObject::connect(&instance, &bazarish::app::SingleInstance::linkRequested, &controller,
        &bazarish::app::AppController::openLink);
    links.handOverTo(controller);
    if (!link.isEmpty()) {
        controller.openLink(link);
    }
    QObject::connect(&controller, &bazarish::app::AppController::raiseRequested, &app, raiseWindow);

#ifndef Q_OS_ANDROID
    bazarish::app::Notifier notifier(core);
    QObject::connect(&core, &bazarish::app::CoreHost::readyToExit, &app, exitNow);
    std::unique_ptr<bazarish::app::TrayIcon> tray;
    if (bazarish::app::TrayIcon::available()) {
        tray = std::make_unique<bazarish::app::TrayIcon>(controller);
        QApplication::setQuitOnLastWindowClosed(false);
        tray->attachWindow(qobject_cast<QQuickWindow*>(engine.rootObjects().first()));
        QObject::connect(tray.get(), &bazarish::app::TrayIcon::quitRequested, &controller,
            &bazarish::app::AppController::prepareForExit);
        QObject::connect(&notifier, &bazarish::app::Notifier::announced, tray.get(),
            &bazarish::app::TrayIcon::showNotice);
    } else {
        bazarish::log::info("tray: this desktop offers none; the window is the only way in");
    }
#endif
    return app.exec();
}
