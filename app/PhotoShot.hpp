// Bazarish project (c) 2026
#pragma once

#include "Picture.hpp"

#include <QImageCapture>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QtQml/qqmlregistration.h>

class QVideoFrame;
class QImage;

namespace bazarish::app {

class PhotoShot : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QImageCapture* capture READ capture WRITE setCapture NOTIFY captureChanged)
    Q_PROPERTY(QString source READ source NOTIFY shotChanged)
public:
    explicit PhotoShot(QObject* parent = nullptr);
    ~PhotoShot() override;

    QImageCapture* capture() const { return capture_; }
    void setCapture(QImageCapture* capture);

    QString source() const;

    const PreparedPicture& picture() const { return picture_; }

    Q_INVOKABLE void take();
    Q_INVOKABLE void discard();

signals:
    void captureChanged();
    void shotChanged();
    void failed(const QString& reason);

private:
    void hold(const QVideoFrame& frame);
    void publish(const QImage& image);
    void release();

    QPointer<QImageCapture> capture_;
    PreparedPicture picture_;
    QString key_;
};

}  // namespace bazarish::app
