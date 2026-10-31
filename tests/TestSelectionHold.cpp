// Bazarish project (c) 2026
#include "SelectionHold.hpp"

#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickView>
#include <QTest>

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

namespace {

constexpr int kWordStart = 6;
constexpr int kWordEnd = 11;
constexpr int kHoldMs = 600;

const char* const kScene = R"(
import QtQuick
Item {
    width: 400; height: 200
    TextEdit { objectName: "edit"; width: 400; height: 100; selectByMouse: true; wrapMode: TextEdit.Wrap; text: "first line\nhello world" }
    TextEdit { objectName: "shown"; y: 100; width: 400; height: 100; readOnly: true; selectByMouse: true; text: "hello world" }
}
)";

QString heldAfterRelease(QQuickView& view, QPointingDevice* touch, QQuickItem* field, const int block)
{
    field->forceActiveFocus();
    QMetaObject::invokeMethod(field, "deselect");
    const QPoint at = field->mapToScene(QPointF(30, field->height() * (block + 1) / 3)).toPoint();
    QTest::touchEvent(&view, touch).press(0, at, &view);
    QTest::qWait(kHoldMs);
    const int offset = block == 0 ? 0 : field->property("text").toString().indexOf('\n') + 1;
    CHECK(QMetaObject::invokeMethod(field, "select", Q_ARG(int, offset + kWordStart), Q_ARG(int, offset + kWordEnd)));
    bazarish::app::holdSelection(field);
    QTest::touchEvent(&view, touch).release(0, at, &view);
    QTest::qWait(kHoldMs / 4);
    return field->property("selectedText").toString();
}

}  // namespace

int main(int argc, char** argv)
{
    QGuiApplication app(argc, argv);
    QQuickView view;
    QQmlComponent component(view.engine());
    component.setData(kScene, QUrl());
    QQuickItem* const root = qobject_cast<QQuickItem*>(component.create());
    CHECK(root != nullptr);
    view.setContent(QUrl(), &component, root);
    view.resize(400, 200);
    view.show();
    CHECK(QTest::qWaitForWindowExposed(&view));
    QPointingDevice* const touch = QTest::createTouchDevice();
    QQuickItem* const edit = root->findChild<QQuickItem*>("edit");
    QQuickItem* const shown = root->findChild<QQuickItem*>("shown");
    CHECK(edit != nullptr && shown != nullptr);

    CHECK(heldAfterRelease(view, touch, edit, 1) == "world");
    CHECK(heldAfterRelease(view, touch, shown, 0) == "world");

    QObject notAField;
    bool refused = false;
    try {
        bazarish::app::holdSelection(&notAField);
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    CHECK(refused);
    std::printf("TestSelectionHold: ok\n");
    return 0;
}
