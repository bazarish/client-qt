// Bazarish project (c) 2026
#include "AppController.hpp"
#include "AvatarStore.hpp"
#include "PictureStore.hpp"
#include "I2pController.hpp"
#include "Identicon.hpp"

#include <QColor>
#include <QFile>
#include <QFont>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QIcon>
#include <QImage>
#include <QLibraryInfo>
#include <QPalette>
#include <QPixmap>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QUrl>

#include <array>
#include <utility>

int main(int argc, char** argv)
{
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

    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName("Bazarish");
    QGuiApplication::setOrganizationName("Bazarish");

    // A neutral base style; the visual language is defined by the QML Theme
    // (the brand's terminal/neon look).
    QQuickStyle::setStyle("Basic");

    // The brand is monospace everywhere: load the bundled Roboto Mono and make
    // it the application-wide default so every control inherits it.
    QFontDatabase::addApplicationFont(":/fonts/RobotoMono-Regular.ttf");
    QFontDatabase::addApplicationFont(":/fonts/RobotoMono-Bold.ttf");
    // Bundle a colour-emoji font (Twemoji Mozilla, COLR/CPAL) so reactions render in
    // colour regardless of the system fonts. Roboto Mono has no emoji glyphs, and a
    // monochrome fallback would be invisible on the dark theme; the UI selects this
    // family with Text.NativeRendering where it shows emoji (a colour font needs the
    // native rasterizer - Qt's default distance-field text is monochrome only).
    QFontDatabase::addApplicationFont(":/fonts/TwemojiMozilla.ttf");
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
    return app.exec();
}
