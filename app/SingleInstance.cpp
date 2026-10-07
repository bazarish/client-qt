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

constexpr int kHandoverWaitMs = 1000;
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
    if (!lock_->tryLock()) {
        if (lock_->error() != QLockFile::LockFailedError) {
            bazarish::log::warn("account folder could not be locked: QLockFile error {}",
                static_cast<int>(lock_->error()));
        }
        lock_.reset();
        return false;
    }
    server_ = std::make_unique<QLocalServer>();
    QLocalServer::removeServer(socketName());
    if (!server_->listen(socketName())) {
        bazarish::log::warn("another copy will not be able to hand over: {}",
            server_->errorString().toStdString());
        server_.reset();
        return true;
    }
    connect(server_.get(), &QLocalServer::newConnection, this, [this]() {
        while (QLocalSocket* const client = server_->nextPendingConnection()) {
            // What it sent stays readable after it hangs up, so one read is enough.
            connect(client, &QLocalSocket::disconnected, this, [this, client]() {
                const QString link = QString::fromUtf8(client->readAll()).trimmed();
                client->deleteLater();
                if (!link.isEmpty()) {
                    emit linkRequested(link);
                }
            });
            emit showRequested();
        }
    });
    return true;
}

bool SingleInstance::handOver(const QString& link)
{
    QLocalSocket socket;
    socket.connectToServer(socketName());
    if (!socket.waitForConnected(kHandoverWaitMs)) {
        return false;
    }
    if (!link.isEmpty()) {
        socket.write(link.toUtf8());
        if (!socket.waitForBytesWritten(kHandoverWaitMs)) {
            bazarish::log::warn("the running copy was reached but not told what to open");
            return false;
        }
    }
    socket.disconnectFromServer();
    return true;
}

}  // namespace bazarish::app
