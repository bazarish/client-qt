// Bazarish project (c) 2026
#include "SessionController.hpp"

#include "SessionShared.hpp"





#include "DeliveryStatus.hpp"
#include "Session.hpp"

#include <bazarish/Crypto.hpp>
#include <bazarish/Limits.hpp>
#include <bazarish/Descriptor.hpp>
#include <bazarish/Portal.hpp>

// Qt makes `emit` a macro and the log header declares a function of that name,
// so the keyword is stood down for the length of this include.
#pragma push_macro("emit")
#undef emit
#include <bazarish/Log.hpp>
#pragma pop_macro("emit")

#include <QByteArray>
#include <chrono>
#include <QTimer>
#include <cstring>

#if defined(Q_OS_LINUX) && defined(BAZARISH_HAVE_QTDBUS)
#endif

#include <algorithm>
#include <array>
#include <ctime>
#include <exception>
#include <fstream>
#include <iterator>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

namespace bazarish::app {

namespace {
// A voice message rides inside one message, so what really bounds it is the
// payload cap, not the clock: recording stops once the encoded audio has spent
// its share. The reserve covers the message around it (ids, reply, the CBOR
// keys), which is far smaller than this but must not be cut fine.
constexpr qint64 kMaxVoiceMs = 2 * 60 * 1000;
constexpr std::size_t kVoiceEnvelopeReserveBytes = 8 * 1024;
constexpr std::size_t kMaxVoiceBytes
    = bazarish::kMaxMessagePayloadBytes - kVoiceEnvelopeReserveBytes;
// Below this it is a slip of the finger, not a message.
constexpr qint64 kMinVoiceMs = 700;
// How often the recording clock and the input level are reported to the UI: the
// level is a live picture of the microphone, so it is sampled at a rate a user
// reads as movement rather than as steps.
constexpr int kVoiceTickMs = 50;
// How many bars a voice message's drawn waveform has - enough shape to read at
// the width of a bubble.
// The speeds a voice message plays back at, stepped through by the bubble's own
// control. Faster playback raises the pitch with it: the samples are handed to
// the device faster, and nothing time-stretches them back.
constexpr std::array<double, 3> kVoiceSpeeds = {1.0, 1.5, 2.0};
}  // namespace

using bazarish::client::IncomingMessage;
using bazarish::client::ServerEndpoint;
using bazarish::client::Session;

// Voice messages: recording, the take waiting to be sent, and playback.

// The one VoiceNote this controller records and plays through, built on first
// use. Both playback kinds end on the same signal, so both are cleared there.
VoiceNote* SessionController::voiceNote()
{
    if (!voice_) {
        voice_ = std::make_unique<VoiceNote>();
        connect(voice_.get(), &VoiceNote::playbackFinished, this, [this]() {
            const QString finished = voicePlaying_;
            voicePlaying_.clear();
            voiceTakePlaying_ = false;
            playbackTimer_.stop();
            voicePositionMs_ = 0;
            emit voiceChanged();
            // Run on to the next voice message in this chat, from either side:
            // a run of them is one thing to listen to, not a row of buttons.
            if (finished.isEmpty() || activePeer_.isEmpty()) {
                return;
            }
            // By protocol id in this conversation, either direction: the
            // outgoing-only lookup that serves delivery receipts found nothing
            // for a message we had received, and the run stopped at the first one.
            const qint64 playedId = store_.messageByE2e(finished, activePeer_).id;
            if (playedId == 0) {
                return;
            }
            const StoredMessage next = store_.nextVoiceAfter(activePeer_, playedId);
            if (!next.e2eId.isEmpty()) {
                playVoice(next.e2eId, 0);
            }
        });
        playbackTimer_.setInterval(kVoiceTickMs);
        connect(&playbackTimer_, &QTimer::timeout, this, [this]() {
            voicePositionMs_ = voice_->playbackPositionMs();
            emit voiceChanged();
        });
        voiceTimer_.setInterval(kVoiceTickMs);
        connect(&voiceTimer_, &QTimer::timeout, this, [this]() {
            voiceLevel_ = voice_->inputLevel();
            if (!voiceRecording_) {
                emit voiceChanged();  // watching the microphone, not filling a take
                return;
            }
            voiceElapsedMs_ = voice_->elapsedMs();
            emit voiceChanged();
            // Full is full, by weight or by the clock. Recording stops on its
            // own - the take is kept, and the user still decides whether it goes.
            if (voiceElapsedMs_ >= kMaxVoiceMs
                || voice_->encodedBytes() >= kMaxVoiceBytes) {
                stopVoiceRecording();
            }
        });
    }
    return voice_.get();
}

void SessionController::startVoiceMonitor()
{
    if (voiceRecording_ || voiceMonitoring_) {
        return;
    }
    voiceError_.clear();
    try {
        voiceNote()->startMonitoring();
    } catch (const std::exception& error) {
        // A microphone that cannot be opened is the very thing this is for.
        voiceError_ = QString::fromUtf8(error.what());
        emit voiceChanged();
        return;
    }
    voiceMonitoring_ = true;
    voiceLevel_ = 0.0;
    voiceTimer_.start();
    emit voiceChanged();
}

void SessionController::stopVoiceMonitor()
{
    if (!voiceMonitoring_) {
        return;
    }
    voiceMonitoring_ = false;
    if (voice_) {
        voice_->stopMonitoring();
    }
    if (!voiceRecording_) {
        voiceTimer_.stop();
    }
    voiceLevel_ = 0.0;
    emit voiceChanged();
}

void SessionController::startVoiceRecording()
{
    if (activePeer_.isEmpty() || voiceRecording_) {
        return;
    }
    // The same microphone cannot be watched and recorded at once.
    stopVoiceMonitor();
    discardVoiceTake();
    voiceError_.clear();
    try {
        voiceNote()->startRecording();
    } catch (const std::exception& error) {
        // Shown in the recorder itself, where the button that failed is.
        voiceError_ = QString::fromUtf8(error.what());
        emit voiceChanged();
        return;
    }
    voiceRecording_ = true;
    voiceElapsedMs_ = 0;
    voiceLevel_ = 0.0;
    voiceTimer_.start();
    emit voiceChanged();
}

void SessionController::stopVoiceRecording()
{
    if (!voiceRecording_ || !voice_) {
        return;
    }
    voiceTimer_.stop();
    voiceRecording_ = false;
    voiceLevel_ = 0.0;
    const qint64 durationMs = voice_->elapsedMs();
    Bytes audio;
    try {
        audio = voice_->stopRecording();
    } catch (const std::exception& error) {
        voiceError_ = QString::fromUtf8(error.what());
        emit voiceChanged();
        return;
    }
    if (audio.empty() || durationMs < kMinVoiceMs) {
        voiceError_ = QStringLiteral("Too short to send.");
        emit voiceChanged();
        return;
    }
    voiceTake_ = QByteArray(
        reinterpret_cast<const char*>(audio.data()), static_cast<qsizetype>(audio.size()));
    voiceTakeMs_ = durationMs;
    voiceTakeWave_ = waveformHex(audio);
    emit voiceChanged();
}

void SessionController::playVoiceTake()
{
    if (voiceTake_.isEmpty()) {
        return;
    }
    if (voiceTakePlaying_) {
        stopVoiceTake();
        return;
    }
    stopVoice();
    voiceTakePlaying_ = true;
    emit voiceChanged();
    try {
        voiceNote()->play(Bytes(voiceTake_.begin(), voiceTake_.end()));
        playbackTimer_.start();
    } catch (const std::exception& error) {
        voiceError_ = QString::fromUtf8(error.what());
        voiceTakePlaying_ = false;
        emit voiceChanged();
    }
}

void SessionController::stopVoiceTake()
{
    if (voice_) {
        voice_->stop();
    }
    playbackTimer_.stop();
    voiceTakePlaying_ = false;
    emit voiceChanged();
}

void SessionController::discardVoiceTake()
{
    if (voiceTakePlaying_) {
        stopVoiceTake();
    }
    voiceTake_.clear();
    voiceTakeMs_ = 0;
    voiceTakeWave_.clear();
    voiceError_.clear();
    emit voiceChanged();
}

void SessionController::sendVoiceTake()
{
    if (voiceTake_.isEmpty() || activePeer_.isEmpty()) {
        return;
    }
    unblockBeforeWriting(activePeer_);
    stopVoiceTake();
    const QByteArray audio = voiceTake_;
    const qint64 durationMs = voiceTakeMs_;
    const QString wave = voiceTakeWave_;
    discardVoiceTake();

    const QString replyTo = replying_ ? replyingE2eId_ : QString();
    if (replying_) {
        cancelReply();
    }
    StoredMessage m;
    m.peer = activePeer_;
    m.outgoing = true;
    m.type = "voice";
    m.e2eId = newE2eId();
    m.replyTo = replyTo;
    m.attMime = QStringLiteral("audio/opus");
    m.attSize = audio.size();
    m.attDurationMs = durationMs;
    m.attWave = wave;
    m.ts = nowMillis();
    m.orderKey = m.ts;
    m.status = DeliveryStatus::Preparing;
    m.id = store_.append(m);
    statusById_[m.id] = DeliveryStatus::Preparing;
    showInActiveView(m, true);
    contacts_.touch(activePeer_, peerName(activePeer_), QStringLiteral("[voice]"), m.ts, true);
    beginOperation(QStringLiteral("send:") + QString::number(m.id), QStringLiteral("send"),
        QStringLiteral("To ") + peerName(activePeer_), QStringLiteral("Sending…"), activePeer_);

    emit requestSendVoice(activePeer_, audio, durationMs, m.id, m.e2eId, replyTo);
}

void SessionController::cancelVoiceRecording()
{
    if (voiceRecording_ && voice_) {
        voiceTimer_.stop();
        voiceRecording_ = false;
        voiceLevel_ = 0.0;
        voice_->cancelRecording();
    }
    discardVoiceTake();
}

qreal SessionController::voiceSpeed() const
{
    return kVoiceSpeeds.at(static_cast<std::size_t>(voiceSpeedStep_));
}

void SessionController::cycleVoiceSpeed()
{
    voiceSpeedStep_ = (voiceSpeedStep_ + 1) % static_cast<int>(kVoiceSpeeds.size());
    emit voiceChanged();
    // A speed chosen mid-playback applies to what is playing, from where it is.
    if (!voicePlaying_.isEmpty()) {
        const QString playing = voicePlaying_;
        stopVoice();
        playVoice(playing);
    }
}

void SessionController::playVoice(const QString& e2eId, const qint64 fromMs)
{
    // The play button on the message that is playing stops it; a tap on its
    // waveform moves playback instead, which is why the position decides.
    if (voicePlaying_ == e2eId && fromMs < 0) {
        stopVoice();
        return;
    }
    stopVoiceTake();
    voiceNote();
    voicePlaying_ = e2eId;
    voiceSeekMs_ = std::max<qint64>(0, fromMs);
    voicePositionMs_ = voiceSeekMs_;
    emit voiceChanged();
    // Read here, like a picture: a press on play must not wait for the worker.
    onVoiceLoaded(e2eId, store_.media(QStringLiteral("voice:") + e2eId));
}

void SessionController::stopVoice()
{
    if (voice_) {
        voice_->stop();
    }
    playbackTimer_.stop();
    voicePositionMs_ = 0;
    voicePlaying_.clear();
    emit voiceChanged();
}

void SessionController::onVoiceLoaded(const QString& e2eId, const QByteArray& bytes)
{
    if (voicePlaying_ != e2eId || !voice_) {
        return;
    }
    try {
        voice_->play(Bytes(bytes.begin(), bytes.end()), voiceSpeed(), voiceSeekMs_);
        playbackTimer_.start();
    } catch (const std::exception& error) {
        // Audio that will not unpack is a broken message, and saying so beats
        // silence from a button that was just pressed.
        emit actionFailed(QStringLiteral("This voice message is broken."));
        bazarish::log::warn("voice audio did not unpack: {}", error.what());
        voicePlaying_.clear();
        emit voiceChanged();
    }
}

}  // namespace bazarish::app
