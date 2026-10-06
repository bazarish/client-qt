// Bazarish project (c) 2026
#include "PhotoShot.hpp"

#include "PictureStore.hpp"
#include "SessionShared.hpp"

#include <QImage>
#include <QVideoFrame>

namespace bazarish::app {

namespace {
const QString kShotKeyPrefix = QStringLiteral("shot:");
const QString kShotName = QStringLiteral("photo");
}  // namespace

PhotoShot::PhotoShot(QObject* const parent)
    : QObject(parent)
{
}

PhotoShot::~PhotoShot()
{
    release();
}

QString PhotoShot::source() const
{
    return key_.isEmpty() ? QString() : QStringLiteral("image://picture/") + key_;
}

void PhotoShot::setCapture(QImageCapture* const capture)
{
    if (capture_ == capture) {
        return;
    }
    if (capture_ != nullptr) {
        disconnect(capture_, nullptr, this, nullptr);
    }
    capture_ = capture;
    if (capture_ != nullptr) {
        connect(capture_, &QImageCapture::imageAvailable, this,
            [this](int, const QVideoFrame& frame) { hold(frame); });
        connect(capture_, &QImageCapture::errorOccurred, this,
            [this](int, QImageCapture::Error, const QString& message) { emit failed(message); });
    }
    emit captureChanged();
}

void PhotoShot::take()
{
    if (capture_ == nullptr) {
        emit failed(QStringLiteral("there is no camera to take a photograph with"));
        return;
    }
    if (capture_->capture() < 0) {
        emit failed(capture_->errorString());
    }
}

void PhotoShot::discard()
{
    if (key_.isEmpty()) {
        return;
    }
    release();
    emit shotChanged();
}

void PhotoShot::release()
{
    if (key_.isEmpty()) {
        return;
    }
    PictureStore::instance().remove(key_);
    key_.clear();
    picture_ = {};
}

void PhotoShot::hold(const QVideoFrame& frame)
{
    // Delivered from inside the camera's own capture: anything the new still
    // sets off - stopping the camera among it - has to wait for that to return.
    const QImage image = frame.toImage();
    QMetaObject::invokeMethod(
        this, [this, image]() { publish(image); }, Qt::QueuedConnection);
}

void PhotoShot::publish(const QImage& image)
{
    release();
    picture_ = preparePicture(image, kShotName);
    if (picture_.isEmpty()) {
        emit failed(QStringLiteral("the camera's frame could not be read"));
        emit shotChanged();
        return;
    }
    const QString key = kShotKeyPrefix + newE2eId();
    if (!PictureStore::instance().put(key, picture_.bytes)) {
        picture_ = {};
        emit failed(QStringLiteral("the photograph could not be held"));
        emit shotChanged();
        return;
    }
    key_ = key;
    emit shotChanged();
}

}  // namespace bazarish::app
