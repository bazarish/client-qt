// Bazarish project (c) 2026
#include "Models.hpp"
#include "TestUtil.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickView>
#include <QStyleHints>
#include <QTemporaryDir>
#include <QTest>
#include <QWheelEvent>

#include <cmath>
#include <cstdio>
#include <vector>

using bazarish::app::ConversationModel;
using bazarish::app::StoredMessage;
using bazarish::app::ViewLanding;

namespace {

constexpr int kPageRows = 25;
constexpr int kLongEvery = 3;
constexpr int kLongWords = 30;
constexpr int kPictureEvery = 10;
constexpr int kPictureAt = 9;
constexpr int kSettleMs = 600;
constexpr int kFlashForgetMs = 2400;
constexpr double kPixel = 1.0;
// Qt: a wheel notch turns by 120 eighths of a degree, a line by 24 px.
constexpr int kNotchAngle = 120;
constexpr double kLinePixels = 24;
constexpr int kQuickNotches = 4;
constexpr int kBetweenNotchesMs = 50;
constexpr qint64 kSentAt = 1'700'000'000'000;
constexpr qint64 kFirstChat = 1;
constexpr qint64 kSecondChat = 101;
constexpr qint64 kThirdChat = 201;
constexpr qint64 kOtherAccount = 301;
// Neither has a growing picture above it: a picture that decodes later pushes the rows below it.
constexpr int kUnreadRow = 5;
constexpr int kFoundRow = 6;

int failures = 0;

void expect(const bool ok, const char* what)
{
    std::printf("%s: %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) {
        ++failures;
    }
}

class Fixture : public QObject {
    Q_OBJECT
public:
    Fixture(ConversationModel& first, ConversationModel& second)
        : models_{&first, &second}
    {
    }

    Q_INVOKABLE void fill(const int which, const int count, const qint64 firstId, const int mode,
        const qint64 at)
    {
        QVector<StoredMessage> rows;
        for (qint64 id = firstId; id < firstId + count; ++id) {
            StoredMessage message;
            message.id = id;
            message.orderKey = id;
            message.ts = kSentAt;
            message.peer = QStringLiteral("peer");
            message.e2eId = QStringLiteral("e2e-%1").arg(id);
            message.type = id % kPictureEvery == kPictureAt ? QStringLiteral("image")
                                                            : QStringLiteral("text");
            message.text = QStringLiteral("message %1").arg(id);
            if (id % kLongEvery == 0) {
                message.text += QStringLiteral(" word").repeated(kLongWords);
            }
            rows << message;
        }
        models_.at(which)->setMessages(rows, {static_cast<ViewLanding::Mode>(mode), at});
    }

private:
    std::vector<ConversationModel*> models_;
};

struct Frame {
    double opacity = 0;
    double contentY = 0;
    double endY = 0;
};

double endOf(const QQuickItem* list)
{
    const double originY = list->property("originY").toDouble();
    return std::max(originY - list->property("topMargin").toDouble(),
        originY + list->property("contentHeight").toDouble()
            + list->property("bottomMargin").toDouble() - list->height());
}

bool atEnd(const QQuickItem* list)
{
    return std::abs(list->property("contentY").toDouble() - endOf(list)) < kPixel;
}

int topRow(QQuickItem* list)
{
    int row = -1;
    CHECK(QMetaObject::invokeMethod(list, "indexAt", Q_RETURN_ARG(int, row),
        Q_ARG(qreal, list->width() / 2),
        Q_ARG(qreal,
            list->property("contentY").toDouble() + list->property("topMargin").toDouble() + 2)));
    return row;
}

QQuickItem* itemAt(QQuickItem* list, const int row)
{
    QQuickItem* item = nullptr;
    CHECK(QMetaObject::invokeMethod(
        list, "itemAtIndex", Q_RETURN_ARG(QQuickItem*, item), Q_ARG(int, row)));
    return item;
}

bool bubblesShowTheirRows(QQuickItem* list)
{
    int seen = 0;
    for (QQuickItem* const item : list->property("contentItem").value<QQuickItem*>()->childItems()) {
        const QVariant id = item->property("shownId");
        if (!id.isValid() || !item->isVisible()) {
            continue;
        }
        ++seen;
        if (!item->property("shownText").toString().startsWith(
                QStringLiteral("message %1").arg(id.toLongLong()))) {
            std::printf("  row %lld shows \"%s\"\n", id.toLongLong(),
                qPrintable(item->property("shownText").toString().left(16)));
            return false;
        }
    }
    return seen > 0;
}

void open(QObject* session, const QString& peer, const int which, const qint64 firstId,
    const ViewLanding::Mode mode, const qint64 at)
{
    CHECK(QMetaObject::invokeMethod(session, "open", Q_ARG(QVariant, peer), Q_ARG(QVariant, which),
        Q_ARG(QVariant, kPageRows), Q_ARG(QVariant, firstId), Q_ARG(QVariant, int(mode)),
        Q_ARG(QVariant, at)));
}

bool shownOnlyAtEnd(const std::vector<Frame>& frames)
{
    for (const Frame& frame : frames) {
        if (frame.opacity > 0 && std::abs(frame.contentY - frame.endY) >= kPixel) {
            return false;
        }
    }
    return true;
}

}  // namespace

int main(int argc, char** argv)
try {
    QGuiApplication app(argc, argv);
    const QString viewFile
        = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QStringLiteral(BAZARISH_CONVERSATION_VIEW);
    const QString stubDir
        = argc > 2 ? QString::fromLocal8Bit(argv[2]) : QStringLiteral(BAZARISH_VIEW_STUBS);

    const QTemporaryDir dir;
    CHECK(dir.isValid());
    const QString module = dir.filePath(QStringLiteral("Bazarish"));
    CHECK(QDir().mkpath(module));
    CHECK(QFile::copy(viewFile, module + QStringLiteral("/ConversationView.qml")));
    CHECK(QFile::copy(QStringLiteral(BAZARISH_SCROLL_BOOST), module + QStringLiteral("/ScrollBoost.qml")));
    QString qmldir = QStringLiteral(
        "module Bazarish\nConversationView 1.0 ConversationView.qml\nScrollBoost 1.0 ScrollBoost.qml\n");
    for (const QString& name : QDir(stubDir).entryList({QStringLiteral("*.qml")})) {
        if (name.endsWith(QStringLiteral("Scene.qml"))) {
            CHECK(QFile::copy(stubDir + '/' + name, dir.filePath(name)));
            continue;
        }
        CHECK(QFile::copy(stubDir + '/' + name, module + '/' + name));
        const QString type = name.chopped(4);
        const bool singleton = type == QStringLiteral("Theme") || type == QStringLiteral("App")
            || type == QStringLiteral("Tr");
        qmldir += (singleton ? QStringLiteral("singleton ") : QString()) + type
            + QStringLiteral(" 1.0 ") + name + '\n';
    }
    QFile qmldirFile(module + QStringLiteral("/qmldir"));
    CHECK(qmldirFile.open(QIODevice::WriteOnly));
    CHECK(qmldirFile.write(qmldir.toUtf8()) == qmldir.toUtf8().size());
    qmldirFile.close();

    ConversationModel first;
    ConversationModel second;
    Fixture fixture(first, second);
    QQuickView view;
    view.engine()->addImportPath(dir.path());
    view.rootContext()->setContextProperty(QStringLiteral("firstModel"), &first);
    view.rootContext()->setContextProperty(QStringLiteral("secondModel"), &second);
    view.rootContext()->setContextProperty(QStringLiteral("fixture"), &fixture);
    view.setSource(QUrl::fromLocalFile(dir.filePath(QStringLiteral("Scene.qml"))));
    CHECK(view.status() == QQuickView::Ready);
    view.show();
    CHECK(QTest::qWaitForWindowExposed(&view));

    QObject* const session = view.rootObject()->findChild<QObject*>(QStringLiteral("session"));
    CHECK(session != nullptr);
    QQuickItem* list = nullptr;
    for (QQuickItem* const item : view.rootObject()->findChildren<QQuickItem*>()) {
        if (item->inherits("QQuickListView") && item->property("highlightId").isValid()) {
            list = item;
        }
    }
    CHECK(list != nullptr);

    std::vector<Frame> frames;
    QObject::connect(&view, &QQuickWindow::afterAnimating, &view, [&]() {
        frames.push_back({list->opacity(), list->property("contentY").toDouble(), endOf(list)});
    });

    open(session, QStringLiteral("a"), 0, kFirstChat, ViewLanding::eEnd, 0);
    QTest::qWait(kSettleMs);
    expect(atEnd(list), "a chat opens at its end");
    expect(shownOnlyAtEnd(frames), "no frame of it is drawn away from the end");

    open(session, QStringLiteral("b"), 0, kSecondChat, ViewLanding::eUnread,
        kSecondChat + kUnreadRow);
    QTest::qWait(kSettleMs);
    std::printf("  top row after the switch: %d\n", topRow(list));
    expect(topRow(list) == kUnreadRow, "a switch to a chat with unread opens at the first unread");
    expect(bubblesShowTheirRows(list), "every bubble shows its own row after the switch");

    open(session, QStringLiteral("c"), 0, kThirdChat, ViewLanding::eMessage,
        kThirdChat + kFoundRow);
    QTest::qWait(kSettleMs);
    QQuickItem* const found = itemAt(list, kFoundRow);
    const bool centred = found != nullptr
        && std::abs(found->y() + found->height() / 2 - list->property("contentY").toDouble()
               - list->height() / 2)
            < found->height();
    expect(centred, "a switch to a found message centres it");
    expect(list->property("highlightId").toLongLong() == kThirdChat + kFoundRow,
        "the found message is highlighted");

    frames.clear();
    fixture.fill(1, kPageRows, kOtherAccount, ViewLanding::eEnd, 0);
    CHECK(session->setProperty("conversation", QVariant::fromValue<QObject*>(&second)));
    QTest::qWait(kSettleMs);
    expect(atEnd(list), "another account's chat opens at its end");
    expect(shownOnlyAtEnd(frames), "no frame of it is drawn away from the end");
    expect(bubblesShowTheirRows(list), "every bubble shows its own row after the account switch");

    const double beforeWheel = list->property("contentY").toDouble();
    const QPointF middle(list->width() / 2, list->height() / 2);
    const QPointF inWindow = list->mapToScene(middle);
    QWheelEvent notch(inWindow, view.mapToGlobal(inWindow), QPoint(), QPoint(0, kNotchAngle),
        Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
    QCoreApplication::sendEvent(&view, &notch);
    QTest::qWait(kSettleMs);
    const double afterWheel = list->property("contentY").toDouble();
    const double notchPixels = QGuiApplication::styleHints()->wheelScrollLines() * kLinePixels;
    std::printf("  one notch moved the chat by %.1f px\n", beforeWheel - afterWheel);
    expect(std::abs(beforeWheel - afterWheel - notchPixels) < kPixel,
        "a wheel notch moves the chat by one notch");
    QTest::qWait(kSettleMs);
    expect(std::abs(list->property("contentY").toDouble() - afterWheel) < kPixel,
        "the chat stays put after a wheel notch");

    int tracked = -1;
    CHECK(QMetaObject::invokeMethod(list, "indexAt", Q_RETURN_ARG(int, tracked),
        Q_ARG(qreal, list->width() / 2),
        Q_ARG(qreal, list->property("contentY").toDouble() + list->height() / 2)));
    const auto onScreen = [&]() {
        QQuickItem* const row = itemAt(list, tracked);
        return row == nullptr ? 0.0 : row->y() - list->property("contentY").toDouble();
    };
    const double trackedBefore = onScreen();
    for (int notches = 0; notches < kQuickNotches; ++notches) {
        QWheelEvent quick(inWindow, view.mapToGlobal(inWindow), QPoint(), QPoint(0, kNotchAngle),
            Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QCoreApplication::sendEvent(&view, &quick);
        QTest::qWait(kBetweenNotchesMs);
    }
    QTest::qWait(kSettleMs);
    const double movedOnScreen = onScreen() - trackedBefore;
    std::printf("  %d quick notches moved a row by %.1f px on screen\n", kQuickNotches, movedOnScreen);
    expect(std::abs(movedOnScreen - kQuickNotches * notchPixels) < kPixel,
        "quick notches move the chat by as many notches, no faster");

    QQuickView opened;
    opened.engine()->addImportPath(dir.path());
    opened.rootContext()->setContextProperty(QStringLiteral("firstModel"), &first);
    opened.setSource(QUrl::fromLocalFile(dir.filePath(QStringLiteral("OpenedScene.qml"))));
    CHECK(opened.status() == QQuickView::Ready);
    QObject* const openedSession
        = opened.rootObject()->findChild<QObject*>(QStringLiteral("session"));
    CHECK(openedSession != nullptr);
    QTest::qWait(kFlashForgetMs);
    expect(openedSession->property("flashesForgotten").toInt() > 0,
        "a view made for an already open chat forgets its reaction flashes");

    std::printf("TestConversationView: %s\n", failures == 0 ? "all checks passed" : "FAILED");
    return failures == 0 ? 0 : 1;
} catch (...) {
    return testutil::failed();
}

#include "TestConversationView.moc"
