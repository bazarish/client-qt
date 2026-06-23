// Bazarish project (c) 2026
#include "AppController.hpp"
#include "Identicon.hpp"

#include <QFont>
#include <QFontDatabase>
#include <QGuiApplication>
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
