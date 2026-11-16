// Bazarish project (c) 2026
#include "MentionText.hpp"
#include "TestUtil.hpp"

#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QInputMethodEvent>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QTest>

#include <cstdio>

namespace {

constexpr int kSettleMs = 200;

int failures = 0;

void expect(const bool ok, const char* what)
{
    std::printf("%s: %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) {
        ++failures;
    }
}

class Kit : public QObject {
    Q_OBJECT
public:
    Q_INVOKABLE QVariantMap mentionOffer(
        const QString& before, const QStringList& members, const QString& own) const
    {
        return bazarish::app::mentionOffer(before, members, own);
    }
    Q_INVOKABLE QString mentionText(const QString& name) const
    {
        return bazarish::app::mentionText(name);
    }
    Q_INVOKABLE QStringList namesInOrder(const QStringList& names, const QString& language) const
    {
        return bazarish::app::namesInOrder(names, language);
    }
    Q_INVOKABLE QStringList namesMatching(const QStringList& names, const QString& query) const
    {
        return bazarish::app::namesMatching(names, query);
    }
};

template <typename Wanted>
QList<Wanted*> itemsOf(QObject* root, const char* className)
{
    QList<Wanted*> found;
    for (Wanted* const item : root->findChildren<Wanted*>()) {
        if (item->inherits(className)) {
            found << item;
        }
    }
    return found;
}

QQuickItem* firstItem(QObject* root, const char* className)
{
    const QList<QQuickItem*> items = itemsOf<QQuickItem>(root, className);
    CHECK(!items.isEmpty());
    return items.first();
}

QStringList choicesOf(QQuickItem* composer)
{
    return composer->property("mentionChoices").toStringList();
}

QList<QQuickItem*> shownChoices(QObject* popup)
{
    QList<QQuickItem*> shown;
    auto* const column = popup->property("contentItem").value<QQuickItem*>();
    for (QQuickItem* const row : column->childItems()) {
        if (row->inherits("QQuickItemDelegate") && row->isVisible()) {
            shown << row;
        }
    }
    return shown;
}

void typeText(QWindow* window, const QString& text)
{
    for (const QChar c : text) {
        QTest::sendKeyEvent(QTest::Press, window, Qt::Key_unknown, QString(c), Qt::NoModifier);
        QTest::sendKeyEvent(QTest::Release, window, Qt::Key_unknown, QString(c), Qt::NoModifier);
    }
}

void putText(QQuickItem* input, const QString& text)
{
    CHECK(input->setProperty("text", text));
    CHECK(input->setProperty("cursorPosition", static_cast<int>(text.size())));
    QTest::qWait(kSettleMs);
}

}  // namespace

int main(int argc, char** argv)
try {
    QGuiApplication app(argc, argv);
    const QString qmlDir = QStringLiteral(BAZARISH_QML_DIR);
    const QString stubDir = QStringLiteral(BAZARISH_MENTION_STUBS);

    const QTemporaryDir dir;
    CHECK(dir.isValid());
    const QString module = dir.filePath(QStringLiteral("Bazarish"));
    CHECK(QDir().mkpath(module));
    QString qmldir = QStringLiteral("module Bazarish\n");
    for (const QString& real : {QStringLiteral("Composer"), QStringLiteral("MemberListSheet")}) {
        CHECK(QFile::copy(qmlDir + '/' + real + QStringLiteral(".qml"),
            module + '/' + real + QStringLiteral(".qml")));
        qmldir += real + QStringLiteral(" 1.0 ") + real + QStringLiteral(".qml\n");
    }
    for (const QString& name : QDir(stubDir).entryList({QStringLiteral("*.qml")})) {
        if (name == QStringLiteral("Scene.qml")) {
            CHECK(QFile::copy(stubDir + '/' + name, dir.filePath(name)));
            continue;
        }
        CHECK(QFile::copy(stubDir + '/' + name, module + '/' + name));
        const QString type = name.chopped(4);
        const bool singleton = type == QStringLiteral("Theme") || type == QStringLiteral("App")
            || type == QStringLiteral("Tr") || type == QStringLiteral("TextSelection");
        qmldir += (singleton ? QStringLiteral("singleton ") : QString()) + type
            + QStringLiteral(" 1.0 ") + name + '\n';
    }
    QFile qmldirFile(module + QStringLiteral("/qmldir"));
    CHECK(qmldirFile.open(QIODevice::WriteOnly));
    CHECK(qmldirFile.write(qmldir.toUtf8()) == qmldir.toUtf8().size());
    qmldirFile.close();

    Kit kit;
    QQmlApplicationEngine engine;
    engine.addImportPath(dir.path());
    engine.rootContext()->setContextProperty(QStringLiteral("kit"), &kit);
    engine.load(QUrl::fromLocalFile(dir.filePath(QStringLiteral("Scene.qml"))));
    CHECK(engine.rootObjects().size() == 1);
    auto* const window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
    CHECK(window != nullptr);
    CHECK(QTest::qWaitForWindowExposed(window));

    QObject* const session = window->findChild<QObject*>(QStringLiteral("session"));
    auto* const composer = window->findChild<QQuickItem*>(QStringLiteral("composer"));
    CHECK(session != nullptr && composer != nullptr);
    QQuickItem* const input = firstItem(composer, "QQuickTextEdit");
    QObject* popup = nullptr;
    for (QObject* const candidate : itemsOf<QObject>(composer, "QQuickPopup")) {
        const auto* const content = candidate->property("contentItem").value<QQuickItem*>();
        if (content != nullptr && content->inherits("QQuickColumn")) {
            popup = candidate;
        }
    }
    CHECK(popup != nullptr);
    input->forceActiveFocus();
    QTest::qWait(kSettleMs);

    typeText(window, QStringLiteral("hi @an"));
    QTest::qWait(kSettleMs);
    expect(choicesOf(composer) == QStringList({QStringLiteral("Anna Maria"), QStringLiteral("Annika")}),
        "@an offers the two names that start with it");
    expect(popup->property("visible").toBool() && shownChoices(popup).size() == 2,
        "the list shows both names");

    QTest::keyClick(window, Qt::Key_Tab);
    QTest::qWait(kSettleMs);
    expect(input->property("text").toString() == QStringLiteral("hi @an"),
        "Tab with two names writes nothing");
    expect(input->hasActiveFocus(), "Tab with two names keeps the field focused");

    typeText(window, QStringLiteral("na"));
    QTest::qWait(kSettleMs);
    expect(choicesOf(composer) == QStringList{QStringLiteral("Anna Maria")}, "@anna leaves one name");
    QTest::keyClick(window, Qt::Key_Tab);
    QTest::qWait(kSettleMs);
    expect(input->property("text").toString() == QStringLiteral("hi @Anna_Maria "),
        "Tab writes the mention form and a space");
    expect(!popup->property("visible").toBool(), "the list closes after the choice");

    typeText(window, QStringLiteral("@m"));
    QTest::qWait(kSettleMs);
    expect(choicesOf(composer) == QStringList{QStringLiteral("Mark")}, "the own name is not offered");

    putText(input, QStringLiteral("x @"));
    expect(choicesOf(composer).isEmpty(), "a bare @ offers nothing");
    putText(input, QStringLiteral("mail@b"));
    expect(choicesOf(composer).isEmpty(), "an @ inside a word offers nothing");
    putText(input, QStringLiteral("x (@b"));
    const QList<QQuickItem*> bob = shownChoices(popup);
    expect(bob.size() == 1, "an @ after a bracket offers a name");
    if (bob.size() == 1) {
        const QPointF centre = bob.first()->mapToScene(
            QPointF(bob.first()->width() / 2, bob.first()->height() / 2));
        QTest::mouseClick(window, Qt::LeftButton, {}, centre.toPoint());
        QTest::qWait(kSettleMs);
    }
    expect(input->property("text").toString() == QStringLiteral("x (@Bob "),
        "a click on a name writes it");
    expect(input->hasActiveFocus(), "the click leaves the focus in the field");

    putText(input, QStringLiteral("y @"));
    QInputMethodEvent composing(QString::fromUtf8("е"), {});
    QCoreApplication::sendEvent(input, &composing);
    QTest::qWait(kSettleMs);
    expect(choicesOf(composer) == QStringList{QString::fromUtf8("Елена")},
        "a letter still being composed by a keyboard is taken");

    QObject* const sheet = window->findChild<QObject*>(QStringLiteral("members"));
    CHECK(sheet != nullptr);
    CHECK(QMetaObject::invokeMethod(sheet, "open"));
    QTest::qWait(kSettleMs);
    const QStringList shown = sheet->property("shown").toStringList();
    expect(shown.size() == session->property("activeMembers").toStringList().size(),
        "the member list shows every member");
    const auto before = [&shown](const char* a, const char* b) {
        return shown.indexOf(QString::fromUtf8(a)) < shown.indexOf(QString::fromUtf8(b));
    };
    expect(before("Anna Maria", "Annika") && before("Annika", "Bob") && before("Елена", "ёлка")
            && before("ёлка", "Жора"),
        "the member list is in alphabetical order");
    QQuickItem* const content = sheet->property("contentItem").value<QQuickItem*>();
    QQuickItem* const list = firstItem(content, "QQuickListView");
    expect(list->property("count").toInt() == shown.size(), "every name has a row");

    QQuickItem* search = nullptr;
    for (QQuickItem* const item : content->findChildren<QQuickItem*>()) {
        if (item->property("iconName").toString() == QStringLiteral("search")) {
            search = item;
        }
    }
    CHECK(search != nullptr);
    CHECK(QMetaObject::invokeMethod(search, "clicked"));
    QTest::qWait(kSettleMs);
    typeText(window, QString::fromUtf8("ЛЕН"));
    QTest::qWait(kSettleMs);
    expect(sheet->property("shown").toStringList() == QStringList{QString::fromUtf8("Елена")},
        "search keeps the names that contain the typed letters");
    expect(list->property("count").toInt() == 1, "the list shows the one match");
    CHECK(QMetaObject::invokeMethod(search, "clicked"));
    QTest::qWait(kSettleMs);
    expect(list->property("count").toInt() == shown.size(), "closing the search shows everyone");

    std::printf("TestMentionsUi: %s\n", failures == 0 ? "all checks passed" : "FAILED");
    return failures == 0 ? 0 : 1;
} catch (...) {
    return testutil::failed();
}

#include "TestMentionsUi.moc"
