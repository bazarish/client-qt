// Bazarish project (c) 2026
#pragma once

#include <QImage>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QThreadPool>
#include <QVideoSink>
#include <QtQml/qqmlregistration.h>

#include <atomic>
#include <memory>

struct quirc;

namespace bazarish::app {

class QrScanner : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QVideoSink* sink READ sink WRITE setSink NOTIFY sinkChanged)
public:
    explicit QrScanner(QObject* parent = nullptr);
    ~QrScanner() override;

    QVideoSink* sink() const { return sink_; }
    void setSink(QVideoSink* sink);

    QString read(const QImage& frame);

signals:
    void sinkChanged();
    void decoded(const QString& text);

private:
    void readFrame(const QVideoFrame& frame);

    QPointer<QVideoSink> sink_;
    struct Destroy {
        void operator()(quirc* decoder) const;
    };
    // Its last reader is waited for in the destructor, before this goes.
    std::unique_ptr<quirc, Destroy> decoder_;
    int width_ = 0;
    int height_ = 0;
    QThreadPool reading_;
    // Frames arriving while one is read are let go at once, or the camera runs out of buffers.
    std::atomic<bool> busy_{false};
};

}  // namespace bazarish::app
