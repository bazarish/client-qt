// Bazarish project (c) 2026
#include "AccountCommands.hpp"
#include "AccountEvents.hpp"
#include "AppCommands.hpp"
#include "AppEvents.hpp"
#include "I2pEvents.hpp"
#include "Wire.hpp"

#include <QCoreApplication>
#include <QDataStream>
#include <QElapsedTimer>
#include <QIODevice>
#include <QMetaMethod>
#include <QMetaType>

#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <utility>
#include <vector>

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

constexpr int kSettleMs = 2000;
constexpr qint64 kWideNumber = qint64{1} << 40;

struct Unstreamable {
    int value = 0;
};

bool travels(const QMetaMethod& method)
{
    return method.methodType() == QMetaMethod::Signal
        && (method.attributes() & QMetaMethod::Cloned) == 0;
}

QByteArray encode(const QMetaMethod& method, void** args)
{
    QByteArray bytes;
    QDataStream out(&bytes, QIODevice::WriteOnly);
    out.setVersion(kWireStreamVersion);
    for (int at = 0; at < method.parameterCount(); ++at) {
        CHECK(method.parameterMetaType(at).save(out, args[at + 1]));
    }
    return bytes;
}

void* sample(const QMetaType type, const int seed)
{
    void* const value = type.create();
    switch (type.id()) {
    case QMetaType::QString:
        *static_cast<QString*>(value) = QStringLiteral("text %1").arg(seed);
        break;
    case QMetaType::QByteArray:
        *static_cast<QByteArray*>(value) = QByteArray("bytes ") + QByteArray::number(seed);
        break;
    case QMetaType::Bool:
        *static_cast<bool*>(value) = true;
        break;
    case QMetaType::Int:
        *static_cast<int*>(value) = seed + 7;
        break;
    case QMetaType::LongLong:
        *static_cast<qint64*>(value) = kWideNumber + seed;
        break;
    case QMetaType::ULongLong:
        *static_cast<qulonglong*>(value) = static_cast<qulonglong>(kWideNumber) + seed;
        break;
    case QMetaType::Double:
        *static_cast<double*>(value) = 0.5 + seed;
        break;
    case QMetaType::Float:
        *static_cast<float*>(value) = 1.25F + static_cast<float>(seed);
        break;
    case QMetaType::QStringList:
        *static_cast<QStringList*>(value) = {QStringLiteral("a"), QString::number(seed)};
        break;
    case QMetaType::QVariantMap:
        *static_cast<QVariantMap*>(value) = {{QStringLiteral("k"), seed}};
        break;
    case QMetaType::QVariantList:
        *static_cast<QVariantList*>(value) = {seed, QStringLiteral("x")};
        break;
    default:
        break;
    }
    return value;
}

// No Q_OBJECT: qt_metacall is overridden so one object can hear every signal of another.
class Recorder : public QObject {
public:
    explicit Recorder(QObject& target)
    {
        const QMetaObject* const meta = target.metaObject();
        for (int index = meta->methodOffset(); index < meta->methodCount(); ++index) {
            const QMetaMethod method = meta->method(index);
            if (!travels(method)) {
                continue;
            }
            const int slot = QObject::staticMetaObject.methodCount()
                + static_cast<int>(methods_.size());
            methods_.push_back(method);
            CHECK(QMetaObject::connect(&target, index, this, slot, Qt::DirectConnection));
        }
    }

    int qt_metacall(QMetaObject::Call call, int id, void** args) override
    {
        id = QObject::qt_metacall(call, id, args);
        if (id < 0 || call != QMetaObject::InvokeMetaMethod) {
            return id;
        }
        const QMetaMethod& method = methods_[static_cast<std::size_t>(id)];
        heard.emplace_back(method.methodSignature(), encode(method, args));
        return -1;
    }

    std::vector<std::pair<QByteArray, QByteArray>> heard;

private:
    std::vector<QMetaMethod> methods_;
};

void settleUntil(const std::function<bool()>& done)
{
    QElapsedTimer waited;
    waited.start();
    while (!done() && waited.elapsed() < kSettleMs) {
        QCoreApplication::processEvents();
    }
}

int roundTrip(Relay& from, Relay& to, QObject& sent, QObject& got)
{
    from.sendFrom(QStringLiteral("acct"), sent);
    to.deliverTo(QStringLiteral("acct"), got);
    Recorder recorder(got);
    std::vector<std::pair<QByteArray, QByteArray>> expected;
    const QMetaObject* const meta = sent.metaObject();
    int seed = 0;
    for (int index = meta->methodOffset(); index < meta->methodCount(); ++index) {
        const QMetaMethod method = meta->method(index);
        if (!travels(method)) {
            continue;
        }
        ++seed;
        std::vector<void*> args(static_cast<std::size_t>(method.parameterCount()) + 1, nullptr);
        for (int at = 0; at < method.parameterCount(); ++at) {
            args[static_cast<std::size_t>(at) + 1]
                = sample(method.parameterMetaType(at), seed + at);
        }
        expected.emplace_back(method.methodSignature(), encode(method, args.data()));
        QMetaObject::activate(&sent, method.enclosingMetaObject(), method.relativeMethodIndex(),
            args.data());
        for (int at = 0; at < method.parameterCount(); ++at) {
            method.parameterMetaType(at).destroy(args[static_cast<std::size_t>(at) + 1]);
        }
    }
    settleUntil([&]() { return recorder.heard.size() >= expected.size(); });
    CHECK(recorder.heard == expected);
    from.forget(QStringLiteral("acct"));
    to.forget(QStringLiteral("acct"));
    return static_cast<int>(expected.size());
}

template <typename... Args>
std::pair<QByteArray, QByteArray> heardAs(const QByteArray& signature, const Args&... args)
{
    QByteArray bytes;
    QDataStream out(&bytes, QIODevice::WriteOnly);
    out.setVersion(kWireStreamVersion);
    (out << ... << args);
    return {signature, bytes};
}

void latchedStateReachesALateWindow()
{
    const QString scope = QStringLiteral("acct");
    Relay core;
    AccountEvents sent;
    core.sendFrom(scope, sent, kAccountLatches);
    emit sent.renamed(QStringLiteral("a"));
    emit sent.renamed(QStringLiteral("b"));
    emit sent.avatarReady(QStringLiteral("fp1"), QByteArray("x"));
    emit sent.avatarReady(QStringLiteral("fp2"), QByteArray("y"));
    emit sent.avatarReady(QStringLiteral("fp1"), QByteArray("z"));
    emit sent.actionFailed(QStringLiteral("lost"));

    InProcessChannel channel;
    Relay window;
    window.attach(channel.second());
    AccountEvents got;
    window.deliverTo(scope, got);
    Recorder recorder(got);
    core.attach(channel.first());
    core.replay();
    emit sent.actionFailed(QStringLiteral("live"));
    const std::vector<std::pair<QByteArray, QByteArray>> expected = {
        heardAs("renamed(QString)", QStringLiteral("b")),
        heardAs("avatarReady(QString,QByteArray)", QStringLiteral("fp2"), QByteArray("y")),
        heardAs("avatarReady(QString,QByteArray)", QStringLiteral("fp1"), QByteArray("z")),
        heardAs("actionFailed(QString)", QStringLiteral("live")),
    };
    settleUntil([&]() { return recorder.heard.size() >= expected.size(); });
    CHECK(recorder.heard == expected);

    core.forget(scope);
    InProcessChannel again;
    Relay second;
    second.attach(again.second());
    AccountEvents gotAgain;
    second.deliverTo(scope, gotAgain);
    Recorder nothing(gotAgain);
    core.attach(again.first());
    core.replay();
    QCoreApplication::processEvents();
    CHECK(nothing.heard.empty());
}

QByteArray frameFor(const QString& scope, const QByteArray& signature, const QString& arg)
{
    QByteArray frame;
    QDataStream out(&frame, QIODevice::WriteOnly);
    out.setVersion(kWireStreamVersion);
    out << scope << signature << arg;
    return frame;
}

}  // namespace

class UnstreamableSource : public QObject {
    Q_OBJECT
signals:
    void carries(const Unstreamable& value);
};

int main(int argc, char** argv)
{
    const QCoreApplication app(argc, argv);
    InProcessChannel channel;
    Relay left;
    left.attach(channel.first());
    Relay right;
    right.attach(channel.second());
    int breaks = 0;
    QObject::connect(&right, &Relay::broken, [&breaks](const QString&) { ++breaks; });

    AccountCommands commandsSent;
    AccountCommands commandsGot;
    CHECK(roundTrip(left, right, commandsSent, commandsGot) > 0);
    AccountEvents eventsSent;
    AccountEvents eventsGot;
    CHECK(roundTrip(right, left, eventsSent, eventsGot) > 0);
    AppCommands appCommandsSent;
    AppCommands appCommandsGot;
    CHECK(roundTrip(left, right, appCommandsSent, appCommandsGot) > 0);
    AppEvents appEventsSent;
    AppEvents appEventsGot;
    CHECK(roundTrip(right, left, appEventsSent, appEventsGot) > 0);
    I2pCommands i2pCommandsSent;
    I2pCommands i2pCommandsGot;
    CHECK(roundTrip(left, right, i2pCommandsSent, i2pCommandsGot) > 0);
    I2pEvents i2pEventsSent;
    I2pEvents i2pEventsGot;
    CHECK(roundTrip(right, left, i2pEventsSent, i2pEventsGot) > 0);
    CHECK(breaks == 0);

    AccountEvents target;
    right.deliverTo(QStringLiteral("acct"), target);
    Recorder heard(target);
    const QByteArray good = frameFor(QStringLiteral("acct"), "renamed(QString)", "name");
    channel.first().send(good);
    settleUntil([&]() { return !heard.heard.empty(); });
    CHECK(heard.heard.size() == 1);

    channel.first().send(frameFor(QStringLiteral("acct"), "noSuchSignal(QString)", "x"));
    channel.first().send(good.left(good.size() - 2));
    channel.first().send(good + QByteArray(1, '\0'));
    channel.first().send(frameFor(QStringLiteral("elsewhere"), "renamed(QString)", "name"));
    settleUntil([&]() { return breaks >= 3; });
    QCoreApplication::processEvents();
    CHECK(breaks == 3);
    CHECK(heard.heard.size() == 1);

    UnstreamableSource unstreamable;
    bool refused = false;
    try {
        left.sendFrom(QStringLiteral("bad"), unstreamable);
    } catch (const std::logic_error&) {
        refused = true;
    }
    CHECK(refused);

    latchedStateReachesALateWindow();

    std::fprintf(stderr, "TestWire passed\n");
    return 0;
}

#include "TestWire.moc"
