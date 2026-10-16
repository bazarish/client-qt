// Bazarish project (c) 2026
#pragma once

#include "Wire.hpp"

#include <QByteArray>
#include <QLocalSocket>
#include <QObject>
#include <QString>

#include <memory>

namespace bazarish::app {

QString coreSocketName();

class SocketChannelEnd : public ChannelEnd {
    Q_OBJECT
public:
    explicit SocketChannelEnd(QLocalSocket* socket, QObject* parent = nullptr);

    void send(const QByteArray& frame) override;
    void close();

signals:
    void closed();

private:
    void take();

    QLocalSocket* socket_;
    QByteArray pending_;
};

class CoreLink : public QObject {
    Q_OBJECT
public:
    CoreLink(Relay& relay, const QString& path, QObject* parent = nullptr);

signals:
    void lost();

private:
    Relay& relay_;
    QLocalSocket* socket_;
    SocketChannelEnd* channel_ = nullptr;
    bool connected_ = false;
};

}  // namespace bazarish::app
