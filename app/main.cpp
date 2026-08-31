// Bazarish project (c) 2026
#include "AppController.hpp"
#include "AvatarStore.hpp"
#include "PictureStore.hpp"
#include "I2pController.hpp"
#include "DesktopEntry.hpp"
#include "Identicon.hpp"
#include "SingleInstance.hpp"
#include "TrayIcon.hpp"

#pragma push_macro("emit")
#undef emit
#ifdef _WIN32
// Qt uses std::min and std::max, which the unguarded header takes for itself.
#define NOMINMAX
#include <windows.h>
#endif

#include <bazarish/Log.hpp>
#include <bazarish/ServerDescriptor.hpp>
#pragma pop_macro("emit")

#include <QColor>
#include <QFile>
#include <QFont>
#include <QFontDatabase>
#include <QApplication>
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
#include <QUrl>

#include <array>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <utility>

#ifdef _WIN32

namespace {

// The console flag. Built for the GUI subsystem, this binary starts without a
// console at all, which is what a desktop application should do and also where
// its log would otherwise go.
constexpr const char* kConsoleFlag = "--console";

// Takes the console it was started from when there is one, and opens its own
// otherwise, so the log can be read while the application runs.
void attachConsole()
{
    if (::AttachConsole(ATTACH_PARENT_PROCESS) == 0 && ::AllocConsole() == 0) {
        return;
    }
    if (std::freopen("CONOUT$", "w", stdout) == nullptr
        || std::freopen("CONOUT$", "w", stderr) == nullptr) {
        // There is no log to report this to: the log is what just failed.
        ::MessageBoxA(nullptr, "Could not write to the console.", "Bazarish", MB_ICONWARNING);
    }
}

}  // namespace

#endif

int main(int argc, char** argv)
{
#ifdef _WIN32
    // Before anything that logs: the console has to be there to be written to.
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], kConsoleFlag) == 0) {
            attachConsole();
            break;
        }
    }
#endif

#ifdef _WIN32
    // Colour emoji (the reactions) come out blank under the platform's own font
    // engines: measured here, neither DirectWrite nor GDI draws anything at all
    // for a COLR/CPAL font, while FreeType - what every other platform of this
    // client already rasterises with - draws it. A platform the user named for
    // themselves is left alone.
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
        qputenv("QT_QPA_PLATFORM", "windows:fontengine=freetype");
    }
#endif

    // Qt selects a platform theme from the desktop environment; that theme is what
    // provides the SYSTEM file dialog (the desktop's own chooser, with a pre-filled
    // save name). With no theme named - or one named whose plugin is not there -
    // QtQuick.Dialogs.FileDialog draws its own instead: a different look that does
    // not open the file manager and cannot pre-fill the name. So the theme is
    // picked by what is actually installed beside this build: the desktop portal
    // first (it serves every desktop that runs one), then GTK. A theme the user
    // set, and Qt-native desktops that advertise their own, are left untouched.
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

    // A widgets application, not a plain GUI one: the tray icon and its menu are
    // QtWidgets, and there is no tray without them.
    QApplication app(argc, argv);
    // The one way to talk to a server over anything but I2P, for a stand on a LAN.
    // Named at length so it cannot be turned on by accident or by habit.
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--allow-facade-without-i2p-for-dev-purposes") == 0) {
            bazarish::setAllowFacadeWithoutI2pForDevPurposes(true);
            bazarish::log::warn("talking to facades without I2P: every request leaves this"
                                " machine in the clear. Development only.");
        }
    }
    QGuiApplication::setApplicationName("Bazarish");
    QGuiApplication::setOrganizationName("Bazarish");

    // A neutral base style; the visual language is defined by the QML Theme
    // (the brand's terminal/neon look).
    QQuickStyle::setStyle("Basic");

    // The brand is monospace everywhere: load the bundled Roboto Mono and make
    // it the application-wide default so every control inherits it.
    // A font that does not load is not a cosmetic loss: every glyph then comes
    // from whatever the system offers instead, which is the look this bundles
    // the font to avoid.
    for (const char* const font :
        {":/fonts/RobotoMono-Regular.ttf", ":/fonts/RobotoMono-Bold.ttf"}) {
        if (QFontDatabase::addApplicationFont(QString::fromLatin1(font)) < 0) {
            bazarish::log::warn("brand font not loaded: {}", font);
        }
    }
    // Bundle a colour-emoji font (Twemoji Mozilla, COLR/CPAL) so reactions render in
    // colour regardless of the system fonts. Roboto Mono has no emoji glyphs, and a
    // monochrome fallback would be invisible on the dark theme; the UI selects this
    // family with Text.NativeRendering where it shows emoji (a colour font needs the
    // native rasterizer - Qt's default distance-field text is monochrome only).
    if (QFontDatabase::addApplicationFont(":/fonts/TwemojiMozilla.ttf") < 0) {
        bazarish::log::warn("emoji font not loaded: reactions will be monochrome");
    }
    QFont baseFont("Roboto Mono");
    baseFont.setStyleHint(QFont::Monospace);
    baseFont.setPixelSize(14);
    QGuiApplication::setFont(baseFont);

    // The brand app icon (CRT phosphor "b"). The master is 512x512 and was
    // published at that one size alone, which is not a size window lists and
    // panels ask for; they look for the small ones, and several show nothing at
    // all rather than scale a large icon down themselves.
    constexpr std::array<int, 7> kIconSizes{16, 22, 24, 32, 48, 64, 128};
    const QImage iconMaster(":/icon/bazarish.png");
    QIcon appIcon;
    for (const int size : kIconSizes) {
        appIcon.addPixmap(QPixmap::fromImage(
            iconMaster.scaled(size, size, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
    }
    QGuiApplication::setWindowIcon(appIcon);
    // The desktop entry this application belongs to: where the window itself
    // carries no icon - Wayland, and shells that match windows to installed
    // entries - this name is what they look it up by.
    QGuiApplication::setDesktopFileName("bazarish");
    // ...and, running from an AppImage, make sure that entry exists: a Wayland
    // shell has nothing else to draw the dock icon from.
    try {
        bazarish::app::ensureDesktopEntry();
    } catch (const std::exception& error) {
        // A messenger that will not start because a desktop file could not be
        // written would be the worse failure of the two.
        bazarish::log::warn("desktop entry not installed: {}", error.what());
    }

    // A dark brand palette so default-styled controls are legible: the Basic
    // style reads palette.placeholderText for input placeholders, palette.text
    // for default text, etc. (the QML Theme still drives explicitly-styled
    // surfaces). Without this, placeholders default to a dark, unreadable tone.
    QPalette palette;
    palette.setColor(QPalette::Window, QColor("#16191c"));
    palette.setColor(QPalette::WindowText, QColor("#d7dbd8"));
    palette.setColor(QPalette::Base, QColor("#1b2026"));
    palette.setColor(QPalette::AlternateBase, QColor("#232a31"));
    palette.setColor(QPalette::Text, QColor("#d7dbd8"));
    palette.setColor(QPalette::PlaceholderText, QColor("#8b948c"));
    palette.setColor(QPalette::Button, QColor("#1b2026"));
    palette.setColor(QPalette::ButtonText, QColor("#d7dbd8"));
    palette.setColor(QPalette::Highlight, QColor("#39ff14"));
    palette.setColor(QPalette::HighlightedText, QColor("#11151a"));
    palette.setColor(QPalette::ToolTipBase, QColor("#1b2026"));
    palette.setColor(QPalette::ToolTipText, QColor("#d7dbd8"));
    QGuiApplication::setPalette(palette);

    // One application per account folder: two sharing one folder each hold their
    // own view of the same database and register as the same device, and messages
    // then land in whichever asked first. The lock lives in that folder, so on a
    // first run the folder has to be there before it can be claimed.
    const std::filesystem::path accounts = bazarish::app::AppController::accountsFolder();
    std::filesystem::create_directories(accounts);
    bazarish::app::SingleInstance instance(QString::fromStdString(accounts.string()));
    if (!instance.claim()) {
        const bool handed = instance.handOver();
        bazarish::log::info("another Bazarish already has this account folder; {}",
            handed ? "brought its window forward" : "it is not answering");
        return handed ? 0 : 1;
    }

    QQmlApplicationEngine engine;
    engine.addImageProvider("identicon", new bazarish::app::IdenticonProvider());
    engine.addImageProvider("avatar", new bazarish::app::AvatarProvider());
    engine.addImageProvider("picture", new bazarish::app::PictureProvider());
    engine.addImageProvider("qr", new bazarish::app::QrImageProvider());

    bazarish::app::AppController controller;
    engine.rootContext()->setContextProperty("App", &controller);
    // The shared avatar registry: QML reads Avatars.revision to bust its image
    // cache when an avatar changes (the image://avatar provider reads the store).
    engine.rootContext()->setContextProperty("Avatars", &bazarish::app::AvatarStore::instance());
    engine.rootContext()->setContextProperty(
        "Pictures", &bazarish::app::PictureStore::instance());

    // The embedded I2P router status + persistent on/off setting (read at
    // startup so a previously-disabled router stays off before any session use).
    bazarish::app::I2pController i2pController;
    engine.rootContext()->setContextProperty("I2p", &i2pController);
    // Changing the tunnel profile tears down the destinations already up: left
    // alone they would keep serving at the old hop length for as long as they
    // live, and the setting would look like it had done nothing.
    QObject::connect(&i2pController, &bazarish::app::I2pController::privacyLevelChanged,
        &controller, &bazarish::app::AppController::rebuildI2pLinks);

    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed, &app,
        []() { QCoreApplication::exit(-1); }, Qt::QueuedConnection);

    // The module's own resource path rather than loadFromModule, which arrived in
    // Qt 6.5: this is what that call resolves to, and it builds on 6.4 as well.
    engine.load(QUrl(QStringLiteral("qrc:/qt/qml/Bazarish/app/qml/Main.qml")));
    if (engine.rootObjects().isEmpty()) {
        return -1;
    }

    // The tray, where the desktop has one: the accounts and what they are doing,
    // notifications, and a way back to the window.
    const auto raiseWindow = [&engine]() {
        auto* const window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        if (window == nullptr) {
            return;
        }
        window->setWindowStates(window->windowStates() & ~Qt::WindowMinimized);
        window->show();
        window->raise();
        window->requestActivate();
    };
    // Starting the application again is a request to see it, whether it is behind
    // other windows or has been put away in the tray.
    QObject::connect(&instance, &bazarish::app::SingleInstance::showRequested, &app, raiseWindow);
    // Answering a call from the window it rings in: everything else about a call
    // is in the main window, so it comes forward with the answer.
    QObject::connect(&controller, &bazarish::app::AppController::raiseRequested, &app, raiseWindow);

    std::unique_ptr<bazarish::app::TrayIcon> tray;
    if (bazarish::app::TrayIcon::available()) {
        tray = std::make_unique<bazarish::app::TrayIcon>(controller);
        // Closing the window puts the application in the tray instead of ending it:
        // an account only receives while it runs, and the tray is the way back.
        // Without a tray this would leave no way back at all, so the window keeps
        // being the end of the application there.
        QApplication::setQuitOnLastWindowClosed(false);
        tray->attachWindow(qobject_cast<QQuickWindow*>(engine.rootObjects().first()));
        QObject::connect(tray.get(), &bazarish::app::TrayIcon::quitRequested, &app,
            &QApplication::quit);
    } else {
        bazarish::log::info("tray: this desktop offers none; the window is the only way in");
    }
    return app.exec();
}
