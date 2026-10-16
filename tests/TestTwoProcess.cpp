// Bazarish project (c) 2026
#include "AppCommands.hpp"
#include "AppEvents.hpp"
#include "CoreSocket.hpp"
#include "I2pEvents.hpp"
#include "Wire.hpp"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QProcess>
#include <QTemporaryDir>

#include <cstdio>
#include <cstdlib>
#include <functional>
#include <memory>

#define CHECK(condition)                \
    do {                                \
        if (!(condition)) {             \
            fail(__LINE__, #condition); \
        }                               \
    } while (false)

using namespace bazarish::app;

namespace {

QProcess* gCore = nullptr;

[[noreturn]] void fail(const int line, const char* const condition)
{
    std::fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, line, condition);
    if (gCore != nullptr) {
        gCore->kill();
        gCore->waitForFinished();
    }
    std::exit(1);
}

constexpr int kCoreStartMs = 30000;
constexpr int kSettleMs = 10000;

bool settleUntil(const std::function<bool()>& done, const int limitMs = kSettleMs)
{
    QElapsedTimer waited;
    waited.start();
    while (!done() && waited.elapsed() < limitMs) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    }
    return done();
}

struct Window {
    Relay relay;
    AppCommands commands;
    AppEvents events;
    I2pEvents i2p;
    int started = 0;
    int lists = 0;
    bool i2pState = false;
    std::unique_ptr<CoreLink> link;
    bool lost = false;

    Window()
    {
        relay.sendFrom(QStringLiteral("app"), commands);
        relay.deliverTo(QStringLiteral("app"), events);
        relay.deliverTo(QStringLiteral("i2p"), i2p);
        QObject::connect(&events, &AppEvents::started, [this]() { ++started; });
        QObject::connect(
            &events, &AppEvents::accountList, [this](const QVector<AccountListRow>&) { ++lists; });
        QObject::connect(&i2p, &I2pEvents::state, [this](const I2pState&) { i2pState = true; });
        QObject::connect(&relay, &Relay::broken, [](const QString& reason) {
            std::fprintf(stderr, "broken: %s\n", qPrintable(reason));
            fail(__LINE__, "the relay broke");
        });
    }

    void connectTo(const QString& path)
    {
        link = std::make_unique<CoreLink>(relay, path);
        QObject::connect(link.get(), &CoreLink::lost, [this]() { lost = true; });
        link->open();
    }
};

}  // namespace

int main(int argc, char** argv)
{
    const QCoreApplication app(argc, argv);
    CHECK(argc == 2);
    const QTemporaryDir home;
    CHECK(home.isValid());
    const QString accounts = home.filePath(QStringLiteral("data/accounts"));
    qputenv("BAZARISH_ACCOUNTS_DIR", accounts.toUtf8());
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("HOME"), home.path());
    environment.insert(QStringLiteral("BAZARISH_ACCOUNTS_DIR"), accounts);
    environment.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("offscreen"));

    QProcess core;
    gCore = &core;
    core.setProcessEnvironment(environment);
    bool listening = false;
    QObject::connect(&core, &QProcess::readyReadStandardError, [&core, &listening]() {
        const QByteArray said = core.readAllStandardError();
        std::fwrite(said.constData(), 1, static_cast<std::size_t>(said.size()), stderr);
        listening = listening || said.contains("wire: the core listens");
    });
    core.start(QString::fromLocal8Bit(argv[1]), {});
    CHECK(core.waitForStarted());
    CHECK(settleUntil([&]() { return listening; }, kCoreStartMs));
    const QString path = coreSocketName();

    {
        Window first;
        first.connectTo(path);
        CHECK(settleUntil([&]() { return first.started == 1; }));
        CHECK(first.lists == 1);
        CHECK(first.i2pState);
        first.link.reset();
    }

    Window second;
    second.connectTo(path);
    CHECK(settleUntil([&]() { return second.started == 1; }));
    CHECK(second.lists == 1);
    CHECK(second.i2pState);

    Window third;
    third.connectTo(path);
    CHECK(settleUntil([&]() { return third.started == 1 && second.lost; }));

    emit third.commands.quit();
    CHECK(settleUntil([&]() { return core.state() == QProcess::NotRunning; }));
    CHECK(core.exitStatus() == QProcess::NormalExit);
    CHECK(core.exitCode() == EXIT_SUCCESS);
    CHECK(settleUntil([&]() { return third.lost; }));

    std::fprintf(stderr, "TestTwoProcess passed\n");
    return 0;
}
