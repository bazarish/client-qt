// Bazarish project (c) 2026
#include "PhotoShot.hpp"

#include "PictureStore.hpp"
#include "SessionShared.hpp"

#include <QGuiApplication>
#include <QImage>
#include <QScreen>
#include <QTransform>
#include <QVideoFrame>

#ifdef Q_OS_ANDROID
#include <QJniObject>
#endif

namespace bazarish::app {

namespace {
const QString kShotKeyPrefix = QStringLiteral("shot:");
const QString kShotName = QStringLiteral("photo");
constexpr int kFullTurn = 360;

#ifdef Q_OS_ANDROID
const char* const kWindow = "sh/bazari/client/Window";
#endif

void followTilt(const bool on)
{
#ifdef Q_OS_ANDROID
    QJniObject::callStaticMethod<void>(kWindow, "followTilt", "(Z)V", static_cast<jboolean>(on));
#else
    (void)on;
#endif
}

// What the shot still has to turn so that up in the room is up in it, clockwise.
int turnToUpright()
{
#ifdef Q_OS_ANDROID
    const int held = QJniObject::callStaticMethod<jint>(kWindow, "tilt", "()I");
#else
    const int held = 0;
#endif
    const QScreen* const screen = QGuiApplication::primaryScreen();
    const int shown
        = screen ? screen->angleBetween(screen->nativeOrientation(), screen->orientation()) : 0;
    return ((held - shown) % kFullTurn + kFullTurn) % kFullTurn;
}
}  // namespace

PhotoShot::PhotoShot(QObject* const parent)
    : QObject(parent)
{
}

PhotoShot::~PhotoShot()
{
    if (sink_ != nullptr) {
        followTilt(false);
    }
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
    followTilt(sink_ != nullptr);
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
    const QImage shown = frame.toImage().transformed(turn).mirrored(frame.mirrored(), false);
    QTransform upright;
    upright.rotate(static_cast<qreal>(turnToUpright()));
    publish(shown.transformed(upright));
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
