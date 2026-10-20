// Bazarish project (c) 2026
#pragma once

#include "Picture.hpp"

#include <QObject>
#include <QPointer>
#include <QString>
#include <QVideoSink>
#include <QtQml/qqmlregistration.h>

class QImage;

namespace bazarish::app {

class PhotoShot : public QObject {
    Q_OBJECT
    QML_ELEMENT
    // The still is the preview's frame: a separate still capture re-metered and overexposed.
    Q_PROPERTY(QVideoSink* videoSink READ videoSink WRITE setVideoSink NOTIFY videoSinkChanged)
    Q_PROPERTY(bool ready READ ready NOTIFY readyChanged)
    Q_PROPERTY(QString source READ source NOTIFY shotChanged)
public:
    explicit PhotoShot(QObject* parent = nullptr);
    ~PhotoShot() override;

    QVideoSink* videoSink() const { return sink_; }
    void setVideoSink(QVideoSink* sink);
    bool ready() const { return ready_; }

    QString source() const;

    const PreparedPicture& picture() const { return picture_; }

    Q_INVOKABLE void take();
    Q_INVOKABLE void discard();

signals:
    void videoSinkChanged();
    void readyChanged();
    void shotChanged();
    void failed(const QString& reason);

private:
    void publish(const QImage& image);
    void release();

    QPointer<QVideoSink> sink_;
    bool ready_ = false;
    PreparedPicture picture_;
    QString key_;
};

}  // namespace bazarish::app
