// Bazarish project (c) 2026
#pragma once

#include <QByteArray>
#include <QDataStream>
#include <QHash>
#include <QObject>
#include <QPointer>
#include <QString>

#include <memory>
#include <vector>

namespace bazarish::app {

inline constexpr QDataStream::Version kWireStreamVersion = QDataStream::Qt_6_8;

class ChannelEnd : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;

    virtual void send(const QByteArray& frame) = 0;

signals:
    void received(const QByteArray& frame);
};

class InProcessChannel {
public:
    InProcessChannel();
    ~InProcessChannel();

    InProcessChannel(const InProcessChannel&) = delete;
    InProcessChannel& operator=(const InProcessChannel&) = delete;

    ChannelEnd& first();
    ChannelEnd& second();

private:
    class End;
    std::unique_ptr<End> first_;
    std::unique_ptr<End> second_;
};

class Relay : public QObject {
    Q_OBJECT
public:
    explicit Relay(ChannelEnd& channel, QObject* parent = nullptr);
    ~Relay() override;

    void sendFrom(const QString& scope, QObject& object);
    void deliverTo(const QString& scope, QObject& object);
    void forget(const QString& scope);

signals:
    void broken(const QString& reason);

private:
    class Tap;

    void deliver(const QByteArray& frame);

    ChannelEnd& channel_;
    std::vector<std::unique_ptr<Tap>> taps_;
    QHash<QString, QPointer<QObject>> targets_;
};

}  // namespace bazarish::app
