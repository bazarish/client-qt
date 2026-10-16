// Bazarish project (c) 2026
#pragma once

#include <QByteArray>
#include <QDataStream>
#include <QHash>
#include <QObject>
#include <QPointer>
#include <QString>

#include <cstdint>
#include <map>
#include <memory>
#include <span>
#include <vector>

namespace bazarish::app {

inline constexpr QDataStream::Version kWireStreamVersion = QDataStream::Qt_6_8;

// An event that states something rather than reports it: a window that attaches later is
// told its last value, one per distinct value of its first keyArgs arguments.
struct Latch {
    const char* name;
    int keyArgs = 0;
};

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
    explicit Relay(QObject* parent = nullptr);
    ~Relay() override;

    void attach(ChannelEnd& channel);
    void detach();
    void replay();

    void sendFrom(const QString& scope, QObject& object, std::span<const Latch> latches = {});
    void deliverTo(const QString& scope, QObject& object);
    void forget(const QString& scope);

signals:
    void broken(const QString& reason);

private:
    class Tap;
    struct Latched {
        QString scope;
        QByteArray key;
        QByteArray frame;
    };

    void post(const QString& scope, const QByteArray& frame, const QByteArray* latchKey);
    void deliver(const QByteArray& frame);

    QPointer<ChannelEnd> channel_;
    QMetaObject::Connection receiving_;
    std::vector<std::unique_ptr<Tap>> taps_;
    QHash<QString, QPointer<QObject>> targets_;
    std::uint64_t latchSeq_ = 0;
    std::map<std::uint64_t, Latched> latched_;
    QHash<QByteArray, std::uint64_t> latchedAt_;
};

}  // namespace bazarish::app
