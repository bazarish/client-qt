// Bazarish project (c) 2026
#include "AppController.hpp"
#include "Identicon.hpp"

#include <QColor>
#include <QFont>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QIcon>
#include <QPalette>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>

int main(int argc, char** argv)
{
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
    QFont baseFont("Roboto Mono");
    baseFont.setStyleHint(QFont::Monospace);
    baseFont.setPixelSize(14);
    QGuiApplication::setFont(baseFont);

    // The brand app icon (CRT phosphor "b").
    QGuiApplication::setWindowIcon(QIcon(":/icon/bazarish.png"));

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
    engine.addImageProvider("qr", new bazarish::app::QrImageProvider());

    bazarish::app::AppController controller;
    engine.rootContext()->setContextProperty("App", &controller);

    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed, &app,
        []() { QCoreApplication::exit(-1); }, Qt::QueuedConnection);

    engine.loadFromModule("Bazarish", "Main");
    if (engine.rootObjects().isEmpty()) {
        return -1;
    }
    return app.exec();
}
