// Bazarish project (c) 2026
#include "PhotoShot.hpp"

#include "PictureStore.hpp"
#include "SessionShared.hpp"

#include <QImage>
#include <QTransform>
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

void PhotoShot::setVideoSink(QVideoSink* const sink)
{
    if (sink_ == sink) {
        return;
    }
    if (sink_ != nullptr) {
        disconnect(sink_, nullptr, this, nullptr);
    }
    sink_ = sink;
    ready_ = false;
    if (sink_ != nullptr) {
        connect(sink_, &QVideoSink::videoFrameChanged, this, [this](const QVideoFrame& frame) {
            if (!ready_ && frame.isValid()) {
                ready_ = true;
                emit readyChanged();
            }
        });
    }
    emit videoSinkChanged();
    emit readyChanged();
}

void PhotoShot::take()
{
    const QVideoFrame frame = sink_ != nullptr ? sink_->videoFrame() : QVideoFrame();
    if (!frame.isValid()) {
        emit failed(QStringLiteral("there is no camera to take a photograph with"));
        return;
    }
    // toImage() leaves out the frame's own turn and mirror, which only the preview applies.
    QTransform turn;
    turn.rotate(static_cast<qreal>(frame.rotation()));
    publish(frame.toImage().transformed(turn).mirrored(frame.mirrored(), false));
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
