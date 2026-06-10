// Bazarish project (c) 2026
#include "AppController.hpp"
#include "Identicon.hpp"

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
    // (a calm, minimal, privacy-first look).
    QQuickStyle::setStyle("Basic");

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
