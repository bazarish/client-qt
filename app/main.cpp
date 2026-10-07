// Bazarish project (c) 2026
#include "AppController.hpp"
#include "AvatarStore.hpp"
#include "PictureStore.hpp"
#include "I2pController.hpp"
#include "DesktopEntry.hpp"
#ifdef Q_OS_WIN
#include "UrlScheme.hpp"
#endif
#include "Identicon.hpp"
#include "SingleInstance.hpp"
#include "Translations.hpp"
#include "TrayIcon.hpp"

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
#include <QFont>
#include <QFontDatabase>
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

namespace {
// macOS does not pass a bazarish:// link on the command line: it hands it to the
// running application as an event, and starts no second copy to do it.
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

constexpr const char* kBrandCanvas = "#16191c";
constexpr const char* kBrandSurface = "#1b2026";
constexpr const char* kBrandSurfaceAlt = "#232a31";
constexpr const char* kBrandText = "#d7dbd8";
constexpr const char* kBrandTextDim = "#8b948c";
constexpr const char* kBrandNeon = "#39ff14";
constexpr const char* kBrandAccentInk = "#11151a";

#ifdef Q_OS_LINUX
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

// Qt draws a file chooser of its own unless a platform theme hands it the desktop's.
void useTheDesktopsDialogs()
{
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
}
#endif

}  // namespace

int main(int argc, char** argv)
{
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

    QApplication app(argc, argv);
    LinkCatcher links;
    app.installEventFilter(&links);
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

    for (const char* const font :
        {":/fonts/RobotoMono-Regular.ttf", ":/fonts/RobotoMono-Bold.ttf"}) {
        if (QFontDatabase::addApplicationFont(QString::fromLatin1(font)) < 0) {
            bazarish::log::warn("brand font not loaded: {}", font);
        }
    }
    if (QFontDatabase::addApplicationFont(":/fonts/TwemojiMozilla.ttf") < 0) {
        bazarish::log::warn("emoji font not loaded: reactions will be monochrome");
    }
    QFont baseFont("Roboto Mono");
    baseFont.setStyleHint(QFont::Monospace);
    baseFont.setPixelSize(14);
    QGuiApplication::setFont(baseFont);

    constexpr std::array<int, 7> kIconSizes{16, 22, 24, 32, 48, 64, 128};
    const QImage iconMaster(":/icon/bazarish.png");
    QIcon appIcon;
    for (const int size : kIconSizes) {
        appIcon.addPixmap(QPixmap::fromImage(
            iconMaster.scaled(size, size, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
    }
    QGuiApplication::setWindowIcon(appIcon);
    QGuiApplication::setDesktopFileName("bazarish");
    try {
        bazarish::app::ensureDesktopEntry();
#ifdef Q_OS_WIN
        bazarish::app::ensureUrlScheme();
#endif
    } catch (const std::exception& error) {
        bazarish::log::warn("desktop entry not installed: {}", error.what());
    }

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
    bazarish::app::SingleInstance instance(QString::fromStdString(accounts.string()));
    if (!instance.claim()) {
        const bool handed = instance.handOver(link);
        bazarish::log::info("another Bazarish already has this account folder; {}",
            handed ? "brought its window forward" : "it is not answering");
        return handed ? 0 : 1;
    }

    QQmlApplicationEngine engine;
    bazarish::app::Translations translations(engine);
    QCoreApplication::installTranslator(&translations);
    engine.rootContext()->setContextProperty("Tr", &translations);
    engine.addImageProvider("identicon", new bazarish::app::IdenticonProvider());
    engine.addImageProvider("avatar", new bazarish::app::AvatarProvider());
    engine.addImageProvider("picture", new bazarish::app::PictureProvider());
    engine.addImageProvider("qr", new bazarish::app::QrImageProvider());

    bazarish::app::AppController controller;
    engine.rootContext()->setContextProperty("App", &controller);
    engine.rootContext()->setContextProperty("Avatars", &bazarish::app::AvatarStore::instance());
    engine.rootContext()->setContextProperty(
        "Pictures", &bazarish::app::PictureStore::instance());

    bazarish::app::I2pController i2pController;
    engine.rootContext()->setContextProperty("I2p", &i2pController);
    QObject::connect(&i2pController, &bazarish::app::I2pController::privacyLevelChanged,
        &controller, &bazarish::app::AppController::rebuildI2pLinks);
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

    std::unique_ptr<bazarish::app::TrayIcon> tray;
    if (bazarish::app::TrayIcon::available()) {
        tray = std::make_unique<bazarish::app::TrayIcon>(controller);
        QApplication::setQuitOnLastWindowClosed(false);
        tray->attachWindow(qobject_cast<QQuickWindow*>(engine.rootObjects().first()));
        QObject::connect(tray.get(), &bazarish::app::TrayIcon::quitRequested, &controller,
            &bazarish::app::AppController::prepareForExit);
        QObject::connect(&controller, &bazarish::app::AppController::readyToExit, &app, []() {
            std::fflush(nullptr);
            std::_Exit(0);
        });
    } else {
        bazarish::log::info("tray: this desktop offers none; the window is the only way in");
    }
    return app.exec();
}
