// Bazarish project (c) 2026
#include "AudioRoute.hpp"

#include "AppSettings.hpp"

#include <QList>
#include <QMediaDevices>
#include <QVariantMap>

namespace bazarish::app {

namespace {

QAudioDevice pick(const QList<QAudioDevice>& devices, const QByteArray& id,
    const QAudioDevice& systemDefault)
{
    for (const QAudioDevice& device : devices) {
        if (device.id() == id) {
            return device;
        }
    }
    return systemDefault;
}

QVariantList describe(const QList<QAudioDevice>& devices, const QAudioDevice& chosen)
{
    QVariantList out;
    for (const QAudioDevice& device : devices) {
        out.append(QVariantMap{
            {QStringLiteral("id"), QString::fromUtf8(device.id())},
            {QStringLiteral("name"), device.description()},
            {QStringLiteral("chosen"), device.id() == chosen.id()},
        });
    }
    return out;
}

}  // namespace

AudioRoute& AudioRoute::instance()
{
    static AudioRoute route;
    return route;
}

AudioRoute::AudioRoute()
    : inputId_(QByteArray::fromStdString(AppSettings::instance().audioInput()))
    , outputId_(QByteArray::fromStdString(AppSettings::instance().audioOutput()))
{
}

QAudioDevice AudioRoute::input() const
{
    const std::lock_guard<std::mutex> lock(mutex_);
    return pick(QMediaDevices::audioInputs(), inputId_, QMediaDevices::defaultAudioInput());
}

QAudioDevice AudioRoute::output() const
{
    const std::lock_guard<std::mutex> lock(mutex_);
    return pick(QMediaDevices::audioOutputs(), outputId_, QMediaDevices::defaultAudioOutput());
}

QVariantList AudioRoute::inputs() const
{
    return describe(QMediaDevices::audioInputs(), input());
}

QVariantList AudioRoute::outputs() const
{
    return describe(QMediaDevices::audioOutputs(), output());
}

void AudioRoute::chooseInput(const QString& id)
{
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        inputId_ = id.toUtf8();
    }
    AppSettings::instance().setAudioInput(id.toStdString());
    emit inputChanged();
}

void AudioRoute::chooseOutput(const QString& id)
{
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        outputId_ = id.toUtf8();
    }
    AppSettings::instance().setAudioOutput(id.toStdString());
    emit outputChanged();
}

}  // namespace bazarish::app
