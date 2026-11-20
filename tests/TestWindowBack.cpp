// Bazarish project (c) 2026
#include "TestUtil.hpp"

#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QTest>

#include <cstdio>

namespace {

constexpr int kSettleMs = 300;

int failures = 0;

void expect(const bool ok, const char* what)
{
    std::printf("%s: %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) {
        ++failures;
    }
}

bool opened(QObject* popup)
{
    return popup->property("opened").toBool();
}

void call(QObject* popup, const char* method)
{
    CHECK(QMetaObject::invokeMethod(popup, method));
    QTest::qWait(kSettleMs);
}

void replace(QObject* parentWindow, QObject* child)
{
    call(parentWindow, "open");
    CHECK(opened(parentWindow));
    call(parentWindow, "close");
    CHECK(QMetaObject::invokeMethod(child, "openFrom",
        Q_ARG(QVariant, QVariant::fromValue(parentWindow))));
    QTest::qWait(kSettleMs);
    CHECK(opened(child) && !opened(parentWindow));
}

}  // namespace

int main(int argc, char** argv)
try {
    QGuiApplication app(argc, argv);
    const QString qmlDir = QStringLiteral(BAZARISH_QML_DIR);
    const QString stubDir = QStringLiteral(BAZARISH_WINDOW_BACK_STUBS);

    const QTemporaryDir dir;
    CHECK(dir.isValid());
    const QString module = dir.filePath(QStringLiteral("Bazarish"));
    CHECK(QDir().mkpath(module));
    CHECK(QFile::copy(qmlDir + QStringLiteral("/components/WindowPopup.qml"),
        module + QStringLiteral("/WindowPopup.qml")));
    CHECK(QFile::copy(stubDir + QStringLiteral("/Theme.qml"), module + QStringLiteral("/Theme.qml")));
    CHECK(QFile::copy(
        stubDir + QStringLiteral("/DialogFrame.qml"), module + QStringLiteral("/DialogFrame.qml")));
    CHECK(QFile::copy(stubDir + QStringLiteral("/Scene.qml"), dir.filePath(QStringLiteral("Scene.qml"))));
    const QByteArray qmldir = "module Bazarish\nWindowPopup 1.0 WindowPopup.qml\n"
                              "singleton Theme 1.0 Theme.qml\nDialogFrame 1.0 DialogFrame.qml\n";
    QFile qmldirFile(module + QStringLiteral("/qmldir"));
    CHECK(qmldirFile.open(QIODevice::WriteOnly));
    CHECK(qmldirFile.write(qmldir) == qmldir.size());
    qmldirFile.close();

    QQmlApplicationEngine engine;
    engine.addImportPath(dir.path());
    engine.load(QUrl::fromLocalFile(dir.filePath(QStringLiteral("Scene.qml"))));
    CHECK(engine.rootObjects().size() == 1);
    auto* const window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
    CHECK(window != nullptr);
    CHECK(QTest::qWaitForWindowExposed(window));
    QObject* const parentWindow = window->findChild<QObject*>(QStringLiteral("parentWindow"));
    QObject* const child = window->findChild<QObject*>(QStringLiteral("childWindow"));
    CHECK(parentWindow != nullptr && child != nullptr);

    replace(parentWindow, child);
    call(child, "goBack");
    expect(!opened(child) && opened(parentWindow), "back reopens the window it replaced");
    expect(child->property("returnTo").value<QObject*>() == nullptr, "a closed window forgets its parent");
    call(parentWindow, "close");

    replace(parentWindow, child);
    QTest::keyClick(window, Qt::Key_Escape);
    QTest::qWait(kSettleMs);
    expect(!opened(child) && opened(parentWindow), "Escape goes back");
    call(parentWindow, "close");

    replace(parentWindow, child);
    QTest::keyClick(window, Qt::Key_Back);
    QTest::qWait(kSettleMs);
    expect(!opened(child) && opened(parentWindow), "the system back key goes back");
    call(parentWindow, "close");

    replace(parentWindow, child);
    call(child, "close");
    expect(!opened(child) && !opened(parentWindow), "close leaves the parent closed");

    call(child, "open");
    QTest::keyClick(window, Qt::Key_Escape);
    QTest::qWait(kSettleMs);
    expect(!opened(child) && !opened(parentWindow), "Escape closes a window opened on its own");

    if (failures > 0) {
        std::printf("TestWindowBack: %d failure(s)\n", failures);
        return 1;
    }
    std::printf("TestWindowBack ok\n");
    return 0;
} catch (...) {
    return testutil::failed();
}
