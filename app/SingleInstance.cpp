// Bazarish project (c) 2026
#include "SingleInstance.hpp"

#pragma push_macro("emit")
#undef emit
#include <bazarish/Log.hpp>
#pragma pop_macro("emit")

#include <QCryptographicHash>
#include <QLocalServer>
#include <QLocalSocket>
#include <QLockFile>

namespace bazarish::app {

namespace {

// How long the newcomer waits for the running copy to take the handover. Long
// enough for a busy event loop, short enough that a dead socket does not hold up
// the start.
constexpr int kHandoverWaitMs = 1000;
// The socket is named after the folder, not the user: two folders are two
// applications, and the name must not depend on where the binary came from.
constexpr int kNameBytes = 8;

}  // namespace

SingleInstance::SingleInstance(QString folder, QObject* const parent)
    : QObject(parent)
    , folder_(std::move(folder))
{
}

SingleInstance::~SingleInstance() = default;

QString SingleInstance::socketName() const
{
    const QByteArray digest
        = QCryptographicHash::hash(folder_.toUtf8(), QCryptographicHash::Sha256);
    return QStringLiteral("bazarish-") + QString::fromLatin1(digest.left(kNameBytes).toHex());
}

bool SingleInstance::claim()
{
    lock_ = std::make_unique<QLockFile>(folder_ + QStringLiteral("/.lock"));
    // A copy that was killed leaves its lock behind; QLockFile checks whether the
    // process it names is still alive, so only a live one keeps the folder.
    if (!lock_->tryLock()) {
        lock_.reset();
        return false;
    }
    server_ = std::make_unique<QLocalServer>();
    // The lock is ours, so a socket file left over from a dead copy is ours to
    // clear: without this the listen fails and a second start is never noticed.
    QLocalServer::removeServer(socketName());
    if (!server_->listen(socketName())) {
        bazarish::log::warn("another copy will not be able to hand over: {}",
            server_->errorString().toStdString());
        server_.reset();
        return true;  // the folder is still ours, which is what matters
    }
    connect(server_.get(), &QLocalServer::newConnection, this, [this]() {
        while (QLocalSocket* const client = server_->nextPendingConnection()) {
            client->deleteLater();
            emit showRequested();
        }
    });
    return true;
}

bool SingleInstance::handOver()
{
    QLocalSocket socket;
    socket.connectToServer(socketName());
    if (!socket.waitForConnected(kHandoverWaitMs)) {
        return false;
    }
    socket.disconnectFromServer();
    return true;
}

}  // namespace bazarish::app
