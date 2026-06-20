// Bazarish project (c) 2026
#pragma once

#include "VideoIo.hpp"

#include <QObject>
#include <QVideoFrame>
#include <QVideoSink>

#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>

class QCamera;
class QMediaCaptureSession;

namespace bazarish::app {

// Bridges call video to a QML VideoOutput. Lives in the GUI thread; the call
// engine's worker threads hand it frames through a queued invocation, so the
// QVideoSink is only ever touched on the GUI thread. QML binds a VideoOutput's
// videoSink into the videoSink property and the presenter renders into it.
class VideoPresenter : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVideoSink* videoSink READ videoSink WRITE setVideoSink NOTIFY videoSinkChanged)

public:
    explicit VideoPresenter(QObject* parent = nullptr);

    QVideoSink* videoSink() const { return sink_; }
    void setVideoSink(QVideoSink* sink);

    // Presents one frame; always invoked on the GUI thread.
    void present(const QVideoFrame& frame);

signals:
    void videoSinkChanged();

private:
    QVideoSink* sink_ = nullptr;
};

// Camera capture as a VideoSource: each captured frame is converted to the call
// geometry in I420 for the encoder, and (when set) mirrored to a local-preview
// presenter so the user sees their own camera. Constructed on the worker thread
// when a video call starts, like the audio backend.
class QtVideoSource : public bazarish::VideoSource {
public:
    explicit QtVideoSource(VideoPresenter* localPreview = nullptr);
    ~QtVideoSource() override;

    void start() override;
    void stop() override;
    bazarish::VideoFrame readFrame() override;

private:
    void onFrame(const QVideoFrame& frame);

    std::unique_ptr<QMediaCaptureSession> session_;
    std::unique_ptr<QCamera> camera_;
    std::unique_ptr<QVideoSink> sink_;
    VideoPresenter* localPreview_;

    std::mutex mutex_;
    std::condition_variable cv_;
    bazarish::VideoFrame latest_;
    bool fresh_ = false;
    bool running_ = false;
    std::chrono::steady_clock::time_point lastAccepted_;
};

// Renders incoming call video by handing each decoded I420 frame to a GUI-thread
// VideoPresenter.
class QtVideoSink : public bazarish::VideoSink {
public:
    explicit QtVideoSink(VideoPresenter* presenter);

    void start() override;
    void stop() override;
    void writeFrame(const bazarish::VideoFrame& frame) override;

private:
    VideoPresenter* presenter_;
};

}  // namespace bazarish::app
