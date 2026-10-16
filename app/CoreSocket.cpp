// Bazarish project (c) 2026
#include "CoreSocket.hpp"

#include "AppSettings.hpp"

#pragma push_macro("emit")
#undef emit
#include <bazarish/Log.hpp>
#pragma pop_macro("emit")

#include <QCryptographicHash>
#include <QtEndian>

#include <stdexcept>
#include <string>

namespace bazarish::app {

namespace {

constexpr qsizetype kLengthBytes = sizeof(quint32);
// Far above any frame this client builds: a longer one is a stream that lost its place.
constexpr quint32 kMaxFrameBytes = 16 * 1024 * 1024;
// A window this far behind has stopped reading.
constexpr qint64 kMaxQueuedBytes = 64 * 1024 * 1024;
constexpr int kNameBytes = 8;

}  // namespace

QString coreSocketName()
{
    const QByteArray digest = QCryptographicHash::hash(
        QByteArray::fromStdString(appRoot().string()), QCryptographicHash::Sha256);
    return QStringLiteral("bazarish-core-")
        + QString::fromLatin1(digest.left(kNameBytes).toHex());
}

SocketChannelEnd::SocketChannelEnd(QLocalSocket* const socket, QObject* const parent)
    : ChannelEnd(parent)
    , socket_(socket)
{
    socket_->setParent(this);
    connect(socket_, &QLocalSocket::readyRead, this, &SocketChannelEnd::take);
    connect(socket_, &QLocalSocket::disconnected, this, &SocketChannelEnd::closed);
}

void SocketChannelEnd::send(const QByteArray& frame)
{
    if (socket_->state() != QLocalSocket::ConnectedState) {
        return;
    }
    QByteArray length(kLengthBytes, '\0');
    qToBigEndian(static_cast<quint32>(frame.size()), length.data());
    if (socket_->write(length + frame) < 0) {
        bazarish::log::warn("wire: the socket refused a frame: {}",
            socket_->errorString().toStdString());
        close();
        return;
    }
    if (socket_->bytesToWrite() > kMaxQueuedBytes) {
        bazarish::log::warn("wire: the other end stopped reading; letting it go");
        close();
    }
}

void SocketChannelEnd::close()
{
    socket_->abort();
}

void SocketChannelEnd::take()
{
    pending_ += socket_->readAll();
    while (pending_.size() >= kLengthBytes) {
        const quint32 length = qFromBigEndian<quint32>(pending_.constData());
        if (length > kMaxFrameBytes) {
            bazarish::log::warn("wire: a frame of {} bytes; closing", length);
            pending_.clear();
            close();
            return;
        }
        if (pending_.size() < kLengthBytes + static_cast<qsizetype>(length)) {
            return;
        }
        const QByteArray frame = pending_.sliced(kLengthBytes, length);
        pending_.remove(0, kLengthBytes + length);
        emit received(frame);
    }
}

CoreLink::CoreLink(Relay& relay, const QString& path, QObject* const parent)
    : QObject(parent)
    , relay_(relay)
    , path_(path)
{
}

void CoreLink::open()
{
    relay_.detach();
    if (channel_ != nullptr) {
        channel_->deleteLater();
    }
    auto* const socket = new QLocalSocket;
    channel_ = new SocketChannelEnd(socket, this);
    SocketChannelEnd* const channel = channel_;
    connect(socket, &QLocalSocket::connected, this, [this, channel]() { relay_.attach(*channel); });
    connect(channel, &SocketChannelEnd::closed, this, [this, channel]() {
        if (channel != channel_) {
            return;
        }
        relay_.detach();
        bazarish::log::warn("wire: the core went away");
        emit lost();
    });
    connect(socket, &QLocalSocket::errorOccurred, this,
        [this, channel, socket](const QLocalSocket::LocalSocketError) {
            if (channel != channel_ || socket->state() != QLocalSocket::UnconnectedState
                || relay_.attached()) {
                return;
            }
            bazarish::log::warn("wire: the core is not there: {}",
                socket->errorString().toStdString());
            emit lost();
        });
    socket->connectToServer(path_);
}

}  // namespace bazarish::app
