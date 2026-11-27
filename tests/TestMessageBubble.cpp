// Bazarish project (c) 2026
#include "Models.hpp"
#include "TestUtil.hpp"

#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickView>
#include <QTemporaryDir>
#include <QTest>

#include <cstdio>
#include <mutex>
#include <vector>

using bazarish::app::ConversationModel;
using bazarish::app::StoredMessage;
using bazarish::app::ViewLanding;

namespace {

constexpr int kPageRows = 25;
constexpr int kShortRows = 2;
constexpr int kSettleMs = 400;
constexpr qint64 kSentAt = 1'700'000'000'000;
constexpr qint64 kDayMs = 86'400'000;
constexpr qint64 kRowsPerDay = 4;
constexpr qint64 kFirstChat = 1;
constexpr qint64 kSecondChat = 101;
constexpr qint64 kThirdChat = 201;
constexpr qint64 kLandingRow = 5;
constexpr int kLongEvery = 3;
constexpr int kLongWords = 30;

const QStringList kRealFiles = {QStringLiteral("ConversationView.qml"),
    QStringLiteral("MessageBubble.qml"), QStringLiteral("VoiceTrack.qml"), QStringLiteral("Format.js"),
    QStringLiteral("Placement.js"), QStringLiteral("Theme.qml")};
const QStringList kRealComponents = {QStringLiteral("Label"), QStringLiteral("Icon"),
    QStringLiteral("IconButton"), QStringLiteral("MenuButton"), QStringLiteral("IconLabel"),
    QStringLiteral("ContextMenu"), QStringLiteral("ContextMenuItem"),
    QStringLiteral("CopyableError"), QStringLiteral("Fingerprint"), QStringLiteral("ScrollBoost"),
    QStringLiteral("MiddleElided")};

int failures = 0;
std::mutex warningsMutex;
QStringList warnings;

void expect(const bool ok, const char* what)
{
    std::printf("%s: %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) {
        ++failures;
    }
}

void keepWarnings(QtMsgType type, const QMessageLogContext&, const QString& message)
{
    if (type != QtDebugMsg) {
        const std::lock_guard<std::mutex> lock(warningsMutex);
        warnings << message;
    }
}

int takeWarnings()
{
    const std::lock_guard<std::mutex> lock(warningsMutex);
    for (const QString& line : warnings) {
        std::printf("    %s\n", qPrintable(line));
    }
    const int count = static_cast<int>(warnings.size());
    warnings.clear();
    return count;
}

QVector<StoredMessage> rowsFrom(const qint64 firstId, const int count)
{
    QVector<StoredMessage> rows;
    for (qint64 id = firstId; id < firstId + count; ++id) {
        StoredMessage message;
        message.id = id;
        message.orderKey = id;
        message.ts = kSentAt + id / kRowsPerDay * kDayMs;
        message.peer = QStringLiteral("peer");
        message.e2eId = QStringLiteral("e2e-%1").arg(id);
        message.type = QStringLiteral("text");
        message.outgoing = id % 2 == 0;
        message.text = QStringLiteral("message %1").arg(id);
        if (id % kLongEvery == 0) {
            message.text += QStringLiteral(" word").repeated(kLongWords);
        }
        rows << message;
    }
    return rows;
}

}  // namespace

int main(int argc, char** argv)
try {
    QGuiApplication app(argc, argv);
    const QString qmlDir = QStringLiteral(BAZARISH_QML_DIR);
    const QString stubDir = QStringLiteral(BAZARISH_BUBBLE_STUBS);
    const QString viewStubDir = QStringLiteral(BAZARISH_VIEW_STUBS);

    const QTemporaryDir dir;
    CHECK(dir.isValid());
    const QString module = dir.filePath(QStringLiteral("Bazarish"));
    CHECK(QDir().mkpath(module));
    QString qmldir = QStringLiteral("module Bazarish\n");
    const auto add = [&](const QString& from, const QString& name) {
        CHECK(QFile::copy(from, module + '/' + name));
        if (name.endsWith(QStringLiteral(".qml"))) {
            const QString type = name.chopped(4);
            const bool singleton = type == QStringLiteral("Theme") || type == QStringLiteral("App")
                || type == QStringLiteral("Tr");
            qmldir += (singleton ? QStringLiteral("singleton ") : QString()) + type
                + QStringLiteral(" 1.0 ") + name + '\n';
        }
    };
    for (const QString& name : kRealFiles) {
        add(qmlDir + '/' + name, name);
    }
    for (const QString& type : kRealComponents) {
        add(qmlDir + QStringLiteral("/components/") + type + QStringLiteral(".qml"),
            type + QStringLiteral(".qml"));
    }
    const QStringList own = QDir(stubDir).entryList({QStringLiteral("*.qml")});
    for (const QString& name : QDir(viewStubDir).entryList({QStringLiteral("*.qml")})) {
        const bool replaced = own.contains(name) || name.endsWith(QStringLiteral("Scene.qml"))
            || QFile::exists(module + '/' + name);
        if (!replaced) {
            add(viewStubDir + '/' + name, name);
        }
    }
    for (const QString& name : own) {
        if (name == QStringLiteral("Scene.qml")) {
            CHECK(QFile::copy(stubDir + '/' + name, dir.filePath(name)));
        } else {
            add(stubDir + '/' + name, name);
        }
    }
    QFile qmldirFile(module + QStringLiteral("/qmldir"));
    CHECK(qmldirFile.open(QIODevice::WriteOnly));
    CHECK(qmldirFile.write(qmldir.toUtf8()) == qmldir.toUtf8().size());
    qmldirFile.close();

    ConversationModel rows;
    rows.setMessages(rowsFrom(kFirstChat, kPageRows), {ViewLanding::eEnd, 0});
    qInstallMessageHandler(keepWarnings);
    QQuickView view;
    view.engine()->addImportPath(dir.path());
    view.rootContext()->setContextProperty(QStringLiteral("rows"), &rows);
    view.setSource(QUrl::fromLocalFile(dir.filePath(QStringLiteral("Scene.qml"))));
    CHECK(view.status() == QQuickView::Ready);
    view.show();
    CHECK(QTest::qWaitForWindowExposed(&view));
    QTest::qWait(kSettleMs);
    QObject* const session = view.rootObject()->findChild<QObject*>(QStringLiteral("session"));
    CHECK(session != nullptr);
    QQuickItem* list = nullptr;
    for (QQuickItem* const item : view.rootObject()->findChildren<QQuickItem*>()) {
        if (item->inherits("QQuickListView") && item->property("highlightId").isValid()) {
            list = item;
        }
    }
    CHECK(list != nullptr);
    int shown = 0;
    for (QQuickItem* const item : list->property("contentItem").value<QQuickItem*>()->childItems()) {
        shown += item->property("msgE2eId").toString().startsWith(QStringLiteral("e2e-")) ? 1 : 0;
    }
    std::printf("  bubbles on screen: %d\n", shown);
    expect(shown > 0, "the list draws real bubbles");
    expect(takeWarnings() == 0, "bubbles draw their rows without a warning");

    rows.setMessages(rowsFrom(kThirdChat, kPageRows), {ViewLanding::eUnread, kThirdChat + kLandingRow});
    QTest::qWait(kSettleMs);
    rows.setMessages(rowsFrom(kFirstChat, kPageRows), {ViewLanding::eMessage, kFirstChat + kLandingRow});
    QTest::qWait(kSettleMs);
    expect(takeWarnings() == 0, "a chat opened at its unread or at a found message warns of nothing");

    rows.setMessages(rowsFrom(kThirdChat, kShortRows), {ViewLanding::eEnd, 0});
    QTest::qWait(kSettleMs);
    expect(takeWarnings() == 0, "a switch from the middle of a chat to a shorter one warns of nothing");

    ConversationModel otherRows;
    otherRows.setMessages(rowsFrom(kSecondChat, kPageRows), {ViewLanding::eEnd, 0});
    CHECK(session->setProperty("conversation", QVariant::fromValue<QObject*>(&otherRows)));
    QTest::qWait(kSettleMs);
    expect(takeWarnings() == 0, "another account's rows warn of nothing");
    CHECK(session->setProperty("conversation", QVariant::fromValue<QObject*>(nullptr)));
    QTest::qWait(kSettleMs);
    expect(takeWarnings() == 0, "a closed account's empty chat warns of nothing");

    qInstallMessageHandler(nullptr);
    std::printf("TestMessageBubble: %s\n", failures == 0 ? "all checks passed" : "FAILED");
    return failures == 0 ? 0 : 1;
} catch (...) {
    return testutil::failed();
}
