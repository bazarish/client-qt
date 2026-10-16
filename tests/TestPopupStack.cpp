// Bazarish project (c) 2026
#include "PopupStack.hpp"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QVariant>

#include <cstdio>
#include <cstdlib>

#define CHECK(condition)                                                            \
    do {                                                                            \
        if (!(condition)) {                                                         \
            std::fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__, \
                #condition);                                                        \
            std::exit(1);                                                           \
        }                                                                           \
    } while (false)

using namespace bazarish::app;

namespace {

const char* const kWindow = R"(
import QtQuick
import QtQuick.Controls
ApplicationWindow {
    width: 400
    height: 400
    visible: true
    property alias lower: lower
    property alias upper: upper
    property alias stuck: stuck
    Popup { id: lower; closePolicy: Popup.CloseOnEscape }
    Item { Item { Popup { id: upper; closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside } } }
    Popup { id: stuck; z: 5; closePolicy: Popup.NoAutoClose }
}
)";

QObject* popup(QObject* const window, const char* const name)
{
    return qvariant_cast<QObject*>(window->property(name));
}

bool opened(QObject* const window, const char* const name)
{
    return popup(window, name)->property("opened").toBool();
}

void open(QObject* const window, const char* const name)
{
    CHECK(QMetaObject::invokeMethod(popup(window, name), "open"));
    QCoreApplication::processEvents();
}

bool back(QQuickWindow* const window)
{
    const bool taken = closeTopPopup(window);
    QCoreApplication::processEvents();
    return taken;
}

}  // namespace

int main(int argc, char** argv)
{
    QGuiApplication app(argc, argv);
    QQmlApplicationEngine engine;
    engine.loadData(kWindow);
    CHECK(engine.rootObjects().size() == 1);
    QQuickWindow* const window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
    CHECK(window != nullptr);
    QCoreApplication::processEvents();

    CHECK(!back(window));

    open(window, "lower");
    open(window, "upper");
    CHECK(opened(window, "lower") && opened(window, "upper"));
    CHECK(back(window));
    CHECK(opened(window, "lower") && !opened(window, "upper"));
    CHECK(back(window));
    CHECK(!opened(window, "lower"));
    CHECK(!back(window));

    open(window, "stuck");
    open(window, "lower");
    CHECK(back(window));
    CHECK(opened(window, "stuck") && opened(window, "lower"));

    std::printf("TestPopupStack: all checks passed\n");
    return 0;
}
