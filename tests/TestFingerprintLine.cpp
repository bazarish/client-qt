// Bazarish project (c) 2026
#include "TestUtil.hpp"

#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QStringList>
#include <QTemporaryDir>
#include <QTest>

#include <cstdio>

namespace {

constexpr int kSettleMs = 300;

QStringList g_messages;

void collect(const QtMsgType, const QMessageLogContext&, const QString& message)
{
    g_messages << message;
}

void write(const QString& path, const QByteArray& text)
{
    QFile file(path);
    CHECK(file.open(QIODevice::WriteOnly));
    CHECK(file.write(text) == text.size());
}

bool loopReported()
{
    for (const QString& message : g_messages) {
        if (message.contains(QStringLiteral("Binding loop"))) {
            return true;
        }
    }
    return false;
}

// A narrow window, one line hidden as a closed contact window keeps it and one shown.
const QByteArray kScene = R"(import QtQuick
import QtQuick.Layouts
import Bazarish

Window {
    width: 160
    height: 80
    visible: true
    property string peer: ""
    ColumnLayout {
        anchors.fill: parent
        RowLayout {
            objectName: "hiddenRow"
            visible: false
            Layout.fillWidth: true
            Fingerprint { objectName: "revealed"; Layout.fillWidth: true; value: peer }
        }
        RowLayout {
            Layout.fillWidth: true
            Fingerprint {
                objectName: "shown"
                Layout.fillWidth: true
                value: peer
            }
        }
    }
}
)";

}  // namespace

int main(int argc, char** argv)
try {
    QGuiApplication app(argc, argv);
    const QString qmlDir = QStringLiteral(BAZARISH_QML_DIR);

    const QTemporaryDir dir;
    CHECK(dir.isValid());
    const QString module = dir.filePath(QStringLiteral("Bazarish"));
    CHECK(QDir().mkpath(module));
    for (const char* const name : {"Fingerprint.qml", "Label.qml"}) {
        CHECK(QFile::copy(qmlDir + QStringLiteral("/components/") + QString::fromLatin1(name),
            module + QStringLiteral("/") + QString::fromLatin1(name)));
    }
    write(module + QStringLiteral("/qmldir"),
        "module Bazarish\nFingerprint 1.0 Fingerprint.qml\nLabel 1.0 Label.qml\n");
    write(dir.filePath(QStringLiteral("Scene.qml")), kScene);

    qInstallMessageHandler(collect);
    QQmlApplicationEngine engine;
    engine.addImportPath(dir.path());
    engine.load(QUrl::fromLocalFile(dir.filePath(QStringLiteral("Scene.qml"))));
    CHECK(engine.rootObjects().size() == 1);
    auto* const window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
    CHECK(window != nullptr);
    CHECK(QTest::qWaitForWindowExposed(window));
    QTest::qWait(kSettleMs);
    QObject* const shown = window->findChild<QObject*>(QStringLiteral("shown"));
    QObject* const hiddenRow = window->findChild<QObject*>(QStringLiteral("hiddenRow"));
    CHECK(shown != nullptr && hiddenRow != nullptr);
    CHECK(window->setProperty("peer", QStringLiteral("abcdefghijklmnopqrstuvwxyz234567abcdefghijklmnopqrst")));
    QTest::qWait(kSettleMs);
    CHECK(window->setProperty("peer", QStringLiteral("zyxwvutsrqponmlkjihgfedcba765432zyxwvutsrqponmlkjihg")));
    QTest::qWait(kSettleMs);
    CHECK(hiddenRow->setProperty("visible", true));
    QTest::qWait(kSettleMs);
    qInstallMessageHandler(nullptr);

    for (const QString& message : g_messages) {
        std::printf("qml: %s\n", qPrintable(message));
    }
    CHECK(!loopReported());
    CHECK(shown->property("text").toString().contains(QStringLiteral(" ... ")));
    QObject* const revealed = window->findChild<QObject*>(QStringLiteral("revealed"));
    CHECK(revealed != nullptr);
    CHECK(revealed->property("text").toString().contains(QStringLiteral(" ... ")));
    std::printf("TestFingerprintLine ok\n");
    return 0;
} catch (...) {
    return testutil::failed();
}
