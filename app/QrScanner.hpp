// Bazarish project (c) 2026
#pragma once

#include <QImage>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QVideoSink>
#include <QtQml/qqmlregistration.h>

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
    quirc* decoder_ = nullptr;
    int width_ = 0;
    int height_ = 0;
};

}  // namespace bazarish::app
