// Bazarish project (c) 2026
#include "CoreSocket.hpp"
#include "TestUtil.hpp"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QLocalServer>
#include <QLocalSocket>
#include <QTemporaryDir>
#include <QtEndian>

#include <cstdio>
#include <cstdlib>
#include <functional>
#include <vector>

using namespace bazarish::app;

namespace {

constexpr int kSettleMs = 5000;
constexpr qsizetype kLargeFrameBytes = 1024 * 1024;
constexpr quint32 kTooLongFrameBytes = 16 * 1024 * 1024 + 1;
constexpr qsizetype kStalledFrameBytes = 15 * 1024 * 1024;
constexpr qint64 kQueueLimitBytes = 64 * 1024 * 1024;
constexpr qint64 kStalledReadBufferBytes = 64 * 1024;

void settleUntil(const std::function<bool()>& done)
{
    QElapsedTimer waited;
    waited.start();
    while (!done() && waited.elapsed() < kSettleMs) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 1);
    }
}

QByteArray lengthOf(const quint32 length)
{
    QByteArray bytes(sizeof(quint32), '\0');
    qToBigEndian(length, bytes.data());
    return bytes;
}

struct Pair {
    QLocalSocket* client = nullptr;
    QLocalSocket* served = nullptr;
};

Pair connectPair(QLocalServer& server)
{
    Pair pair;
    pair.client = new QLocalSocket;
    pair.client->connectToServer(server.fullServerName());
    settleUntil([&]() { return server.hasPendingConnections(); });
    pair.served = server.nextPendingConnection();
    CHECK(pair.served != nullptr);
    settleUntil([&]() { return pair.client->state() == QLocalSocket::ConnectedState; });
    CHECK(pair.client->state() == QLocalSocket::ConnectedState);
    return pair;
}

}  // namespace

int main(int argc, char** argv)
{
    const QCoreApplication app(argc, argv);
    const QTemporaryDir dir;
    CHECK(dir.isValid());
    QLocalServer server;
    CHECK(server.listen(dir.filePath(QStringLiteral("wire.sock"))));

    {
        // The window's loop stops right after Android says the app went to the background.
        const Pair pair = connectPair(server);
        SocketChannelEnd window(pair.client);
        window.send(QByteArrayLiteral("inactive"));
        CHECK(pair.served->waitForReadyRead(kSettleMs));
        delete pair.served;
    }

    {
        const Pair pair = connectPair(server);
        SocketChannelEnd window(pair.client);
        SocketChannelEnd core(pair.served);
        std::vector<QByteArray> got;
        QObject::connect(&core, &ChannelEnd::received,
            [&got](const QByteArray& frame) { got.push_back(frame); });
        const std::vector<QByteArray> sent
            = {QByteArray(), QByteArray("small"), QByteArray(kLargeFrameBytes, 'x')};
        for (const QByteArray& frame : sent) {
            window.send(frame);
        }
        settleUntil([&]() { return got.size() >= sent.size(); });
        CHECK(got == sent);
    }

    {
        const Pair pair = connectPair(server);
        SocketChannelEnd core(pair.served);
        std::vector<QByteArray> got;
        QObject::connect(&core, &ChannelEnd::received,
            [&got](const QByteArray& frame) { got.push_back(frame); });
        const QByteArray stream = lengthOf(3) + "one" + lengthOf(3) + "two";
        for (const char byte : stream) {
            pair.client->write(&byte, 1);
            pair.client->flush();
            settleUntil([&]() { return pair.client->bytesToWrite() == 0; });
            QCoreApplication::processEvents();
        }
        settleUntil([&]() { return got.size() >= 2; });
        CHECK(got == (std::vector<QByteArray>{"one", "two"}));
        delete pair.client;
    }

    {
        const Pair pair = connectPair(server);
        SocketChannelEnd core(pair.served);
        bool closed = false;
        int frames = 0;
        QObject::connect(&core, &SocketChannelEnd::closed, [&closed]() { closed = true; });
        QObject::connect(&core, &ChannelEnd::received, [&frames]() { ++frames; });
        pair.client->write(lengthOf(kTooLongFrameBytes) + "x");
        pair.client->flush();
        settleUntil([&]() { return closed; });
        CHECK(closed);
        CHECK(frames == 0);
        delete pair.client;
    }

    {
        // A stalled window is let go, never from inside send.
        const Pair pair = connectPair(server);
        // Windows drains a pipe on a thread of its own unless the reader's buffer is bounded.
        pair.client->setReadBufferSize(kStalledReadBufferBytes);
        SocketChannelEnd core(pair.served);
        bool closed = false;
        QObject::connect(&core, &SocketChannelEnd::closed, [&closed]() { closed = true; });
        const QByteArray frame(kStalledFrameBytes, 's');
        qint64 sent = 0;
        while (sent <= kQueueLimitBytes) {
            core.send(frame);
            sent += frame.size();
            CHECK(!closed);
        }
        settleUntil([&]() { return closed; });
        CHECK(closed);
        delete pair.client;
    }

    std::fprintf(stderr, "TestSocketChannel passed\n");
    return 0;
}
