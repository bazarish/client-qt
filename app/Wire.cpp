// Bazarish project (c) 2026
#include "Wire.hpp"

#pragma push_macro("emit")
#undef emit
#include <bazarish/Log.hpp>
#pragma pop_macro("emit")

#include <QIODevice>
#include <QMetaMethod>
#include <QMetaObject>
#include <QMetaType>

#include <algorithm>
#include <stdexcept>
#include <string>

namespace bazarish::app {

namespace {

bool travels(const QMetaMethod& method)
{
    return method.methodType() == QMetaMethod::Signal
        && (method.attributes() & QMetaMethod::Cloned) == 0;
}

}  // namespace

class InProcessChannel::End : public ChannelEnd {
public:
    void send(const QByteArray& frame) override
    {
        QMetaObject::invokeMethod(
            peer_, [peer = peer_, frame]() { emit peer->received(frame); }, Qt::QueuedConnection);
    }

    End* peer_ = nullptr;
};

InProcessChannel::InProcessChannel()
    : first_(std::make_unique<End>())
    , second_(std::make_unique<End>())
{
    first_->peer_ = second_.get();
    second_->peer_ = first_.get();
}

InProcessChannel::~InProcessChannel() = default;

ChannelEnd& InProcessChannel::first()
{
    return *first_;
}

ChannelEnd& InProcessChannel::second()
{
    return *second_;
}

// No Q_OBJECT: qt_metacall is overridden so one object can listen to every signal of another.
class Relay::Tap : public QObject {
public:
    Tap(Relay& relay, const QString& scope, QObject& source, const std::span<const Latch> latches)
        : relay_(relay)
        , scope_(scope)
    {
        const QMetaObject* const meta = source.metaObject();
        for (int index = meta->methodOffset(); index < meta->methodCount(); ++index) {
            const QMetaMethod method = meta->method(index);
            if (!travels(method)) {
                continue;
            }
            for (int at = 0; at < method.parameterCount(); ++at) {
                const QMetaType type = method.parameterMetaType(at);
                if (!type.isValid() || !type.hasRegisteredDataStreamOperators()) {
                    throw std::logic_error("a signal that cannot cross: "
                        + method.methodSignature().toStdString());
                }
            }
            const int slot = QObject::staticMetaObject.methodCount()
                + static_cast<int>(methods_.size());
            methods_.push_back(method);
            const auto latch = std::find_if(latches.begin(), latches.end(),
                [&method](const Latch& entry) { return method.name() == entry.name; });
            keyArgs_.push_back(latch == latches.end() ? kNotLatched : latch->keyArgs);
            if (!QMetaObject::connect(&source, index, this, slot, Qt::DirectConnection)) {
                throw std::logic_error("a signal that could not be tapped: "
                    + method.methodSignature().toStdString());
            }
        }
    }

    int qt_metacall(QMetaObject::Call call, int id, void** args) override
    {
        id = QObject::qt_metacall(call, id, args);
        if (id < 0 || call != QMetaObject::InvokeMetaMethod) {
            return id;
        }
        if (id < static_cast<int>(methods_.size())) {
            forward(static_cast<std::size_t>(id), args);
        }
        return -1;
    }

    QString scope() const { return scope_; }

private:
    static constexpr int kNotLatched = -1;

    void forward(const std::size_t at, void** args)
    {
        const QMetaMethod& method = methods_[at];
        QByteArray frame;
        QDataStream out(&frame, QIODevice::WriteOnly);
        out.setVersion(kWireStreamVersion);
        out << scope_ << method.methodSignature();
        QByteArray key = method.methodSignature();
        const int keyArgs = keyArgs_[at];
        for (int arg = 0; arg < method.parameterCount(); ++arg) {
            const qsizetype before = frame.size();
            if (!method.parameterMetaType(arg).save(out, args[arg + 1])) {
                throw std::logic_error(
                    "an argument that did not encode: " + method.methodSignature().toStdString());
            }
            if (arg < keyArgs) {
                key += frame.sliced(before);
            }
        }
        relay_.post(scope_, frame, keyArgs == kNotLatched ? nullptr : &key);
    }

    Relay& relay_;
    QString scope_;
    std::vector<QMetaMethod> methods_;
    std::vector<int> keyArgs_;
};

Relay::Relay(QObject* const parent)
    : QObject(parent)
{
}

Relay::~Relay() = default;

void Relay::attach(ChannelEnd& channel)
{
    detach();
    channel_ = &channel;
    receiving_ = connect(&channel, &ChannelEnd::received, this, &Relay::deliver);
}

void Relay::detach()
{
    disconnect(receiving_);
    channel_ = nullptr;
}

void Relay::replay()
{
    if (channel_ == nullptr) {
        return;
    }
    for (const auto& [seq, entry] : latched_) {
        channel_->send(entry.frame);
    }
}

void Relay::post(const QString& scope, const QByteArray& frame, const QByteArray* const latchKey)
{
    if (latchKey != nullptr) {
        const QByteArray key = scope.toUtf8() + '\0' + *latchKey;
        if (const auto was = latchedAt_.constFind(key); was != latchedAt_.cend()) {
            latched_.erase(was.value());
        }
        latchedAt_.insert(key, ++latchSeq_);
        latched_.emplace(latchSeq_, Latched{scope, key, frame});
    }
    if (channel_ != nullptr) {
        channel_->send(frame);
    }
}

void Relay::sendFrom(const QString& scope, QObject& object, const std::span<const Latch> latches)
{
    taps_.push_back(std::make_unique<Tap>(*this, scope, object, latches));
}

void Relay::deliverTo(const QString& scope, QObject& object)
{
    targets_.insert(scope, &object);
}

void Relay::forget(const QString& scope)
{
    targets_.remove(scope);
    std::erase_if(
        taps_, [&scope](const std::unique_ptr<Tap>& tap) { return tap->scope() == scope; });
    for (auto entry = latched_.begin(); entry != latched_.end();) {
        if (entry->second.scope != scope) {
            ++entry;
            continue;
        }
        latchedAt_.remove(entry->second.key);
        entry = latched_.erase(entry);
    }
}

void Relay::deliver(const QByteArray& frame)
{
    QDataStream in(frame);
    in.setVersion(kWireStreamVersion);
    QString scope;
    QByteArray signature;
    in >> scope >> signature;
    if (in.status() != QDataStream::Ok) {
        emit broken(QStringLiteral("a frame that does not read"));
        return;
    }
    QObject* const target = targets_.value(scope);
    if (target == nullptr) {
        bazarish::log::debug("wire: {} arrived for {}, which is gone", signature.toStdString(),
            scope.toStdString());
        return;
    }
    const QMetaObject* const meta = target->metaObject();
    const int index = meta->indexOfSignal(signature.constData());
    if (index < meta->methodOffset()) {
        emit broken(QStringLiteral("a frame names %1, which is not spoken here")
                        .arg(QString::fromLatin1(signature)));
        return;
    }
    const QMetaMethod method = meta->method(index);
    std::vector<void*> args(static_cast<std::size_t>(method.parameterCount()) + 1, nullptr);
    const auto release = [&method, &args]() {
        for (int at = 0; at < method.parameterCount(); ++at) {
            if (void* const held = args[static_cast<std::size_t>(at) + 1]) {
                method.parameterMetaType(at).destroy(held);
            }
        }
    };
    for (int at = 0; at < method.parameterCount(); ++at) {
        const QMetaType type = method.parameterMetaType(at);
        void* const held = type.create();
        args[static_cast<std::size_t>(at) + 1] = held;
        if (held == nullptr || !type.load(in, held)) {
            release();
            emit broken(
                QStringLiteral("a frame for %1 does not read").arg(QString::fromLatin1(signature)));
            return;
        }
    }
    if (in.status() != QDataStream::Ok || !in.atEnd()) {
        release();
        emit broken(
            QStringLiteral("a frame for %1 does not read").arg(QString::fromLatin1(signature)));
        return;
    }
    QMetaObject::activate(target, method.enclosingMetaObject(), method.relativeMethodIndex(),
        args.data());
    release();
}

}  // namespace bazarish::app
