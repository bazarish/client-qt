// Bazarish project (c) 2026
#pragma once

#include <QAudioDevice>
#include <QByteArray>
#include <QObject>
#include <QVariantList>

#include <mutex>

namespace bazarish::app {

class AudioRoute : public QObject {
    Q_OBJECT
public:
    static AudioRoute& instance();

    Q_INVOKABLE QVariantList inputs() const;
    Q_INVOKABLE QVariantList outputs() const;
    Q_INVOKABLE void chooseInput(const QString& id);
    Q_INVOKABLE void chooseOutput(const QString& id);

    QAudioDevice input() const;
    QAudioDevice output() const;
    QString inputId() const;
    QString outputId() const;

signals:
    void inputChanged();
    void outputChanged();

private:
    AudioRoute();

    mutable std::mutex mutex_;
    QByteArray inputId_;
    QByteArray outputId_;
};

}  // namespace bazarish::app
