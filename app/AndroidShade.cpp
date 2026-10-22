// Bazarish project (c) 2026
#include "AndroidShade.hpp"

#include "AndroidCallAudio.hpp"
#include "CoreHost.hpp"
#include "Notifier.hpp"
#include "Translations.hpp"

#include <QJniEnvironment>
#include <QJniObject>
#include <QtCore/qcoreapplication_platform.h>

#pragma push_macro("emit")
#undef emit
#include <bazarish/Log.hpp>
#pragma pop_macro("emit")

#include <android/log.h>
#include <unistd.h>

#include <atomic>
#include <cstdio>
#include <functional>
#include <iterator>
#include <stdexcept>
#include <string>
#include <thread>

namespace bazarish::app {
namespace {

const char* const kShade = "sh/bazari/client/Shade";
const char* const kLogTag = "bazarish";
constexpr int kLogChunkBytes = 1024;

std::atomic<CoreHost*> gCore{nullptr};

QJniObject javaString(const QString& text)
{
    return QJniObject::fromString(text);
}

// The shade's buttons are pressed on Android's own thread; the core answers on its own.
void inCoreThread(std::function<void(CoreHost&)> action)
{
    CoreHost* const core = gCore.load();
    if (core == nullptr) {
        return;
    }
    if (!QMetaObject::invokeMethod(
            core, [core, action = std::move(action)]() { action(*core); },
            Qt::QueuedConnection)) {
        bazarish::log::warn("android: a button pressed in the shade did not reach the core");
    }
}

void notificationsToggled(JNIEnv*, jclass)
{
    inCoreThread(
        [](CoreHost& core) { core.setNotificationsEnabled(!core.notificationsEnabled()); });
}

void quitRequested(JNIEnv*, jclass)
{
    inCoreThread([](CoreHost& core) { core.prepareForExit(); });
}

void callDeclined(JNIEnv*, jclass)
{
    inCoreThread([](CoreHost& core) { core.declineRinging(); });
}

void networkChanged(JNIEnv*, jclass)
{
    inCoreThread([](CoreHost& core) { core.networkChanged(); });
}

}  // namespace

AndroidShade::AndroidShade(CoreHost& core, Notifier& notifier, Translations& translations,
    QObject* const parent)
    : QObject(parent)
    , core_(core)
{
    const JNINativeMethod natives[] = {
        {"notificationsToggled", "()V", reinterpret_cast<void*>(notificationsToggled)},
        {"quitRequested", "()V", reinterpret_cast<void*>(quitRequested)},
        {"callDeclined", "()V", reinterpret_cast<void*>(callDeclined)},
        {"networkChanged", "()V", reinterpret_cast<void*>(networkChanged)},
    };
    QJniEnvironment env;
    if (!env.registerNativeMethods(kShade, natives, static_cast<int>(std::size(natives)))) {
        throw std::runtime_error("the Android shade did not take its native methods");
    }
    gCore.store(&core);
    QJniObject::callStaticMethod<void>(kShade, "nativesRegistered", "()V");

    line_ = presenceLine();
    show();
    // A call notice left by a process that is gone belongs to a call that is gone.
    QJniObject::callStaticMethod<void>(kShade, "cancelCall", "()V");

    connect(&notifier, &Notifier::announced, this, &AndroidShade::notifyMessage);
    connect(&notifier, &Notifier::arrivedInSight, this, &AndroidShade::buzzForMessage);
    connect(&core_, &CoreHost::notificationsEnabledChanged, this, &AndroidShade::show);
    connect(&core_, &CoreHost::accountsChanged, this, [this]() {
        const QString line = presenceLine();
        if (line != line_) {
            line_ = line;
            show();
        }
    });
    connect(&translations, &Translations::languageChanged, this, &AndroidShade::show);
    connect(&core_, &CoreHost::ringingChanged, this, &AndroidShade::followRinging);
    connect(&core_, &CoreHost::callLiveChanged, this, &AndroidShade::followRinging);
    connect(&core_, &CoreHost::speakerOnChanged, this, &AndroidShade::followRinging);
    connect(&core_, &CoreHost::windowActiveChanged, this, &AndroidShade::followRinging);
}

AndroidShade::~AndroidShade()
{
    gCore.store(nullptr);
}

void AndroidShade::routeStderrToLogcat()
{
    int ends[2] = {-1, -1};
    if (pipe(ends) != 0 || dup2(ends[1], STDERR_FILENO) < 0) {
        throw std::runtime_error("stderr could not be turned towards logcat");
    }
    close(ends[1]);
    std::setvbuf(stderr, nullptr, _IOLBF, 0);
    // Not a FILE: fgets holds its lock while it waits, and fflush(nullptr) at exit waits for it.
    std::thread([reading = ends[0]]() {
        std::string pending;
        char chunk[kLogChunkBytes];
        ssize_t got = 0;
        while ((got = read(reading, chunk, sizeof(chunk))) > 0) {
            pending.append(chunk, static_cast<std::size_t>(got));
            std::size_t end = 0;
            while ((end = pending.find('\n')) != std::string::npos) {
                __android_log_write(ANDROID_LOG_INFO, kLogTag, pending.substr(0, end).c_str());
                pending.erase(0, end + 1);
            }
        }
    }).detach();
}

void AndroidShade::startRinging()
{
    QJniObject::callStaticMethod<void>(kShade, "startRinging", "()V");
}

void AndroidShade::stopRinging()
{
    QJniObject::callStaticMethod<void>(kShade, "stopRinging", "()V");
}

void AndroidShade::buzzForMessage()
{
    QJniObject::callStaticMethod<void>(kShade, "buzzForMessage", "()V");
}

void AndroidShade::listening()
{
    QJniObject::callStaticMethod<void>(kShade, "listening", "()V");
}

void AndroidShade::stop()
{
    QJniObject::callStaticMethod<void>(kShade, "stop", "()V");
}

QString AndroidShade::presenceLine() const
{
    int unread = 0;
    int open = 0;
    bool connected = true;
    for (const AccountRow& account : core_.accountStatuses()) {
        unread += account.unread;
        if (account.open) {
            ++open;
            connected = connected && account.connected;
        }
    }
    if (unread > 0) {
        return tr("Unread: %1").arg(unread);
    }
    if (open == 0) {
        return tr("Offline");
    }
    return connected ? tr("Online") : tr("Connecting…");
}

void AndroidShade::show()
{
    const QJniObject presence = javaString(tr("Background work"));
    const QJniObject messages = javaString(tr("Messages"));
    const QJniObject reactions = javaString(tr("Reactions"));
    const QJniObject calls = javaString(tr("Calls"));
    const QJniObject turnOff = javaString(tr("Turn off notifications"));
    const QJniObject turnOn = javaString(tr("Turn on notifications"));
    const QJniObject quit = javaString(tr("Quit"));
    const QJniObject line = javaString(line_);
    QJniObject::callStaticMethod<void>(kShade, "show",
        "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;"
        "Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;ZLjava/lang/String;)V",
        presence.object<jstring>(), messages.object<jstring>(), reactions.object<jstring>(),
        calls.object<jstring>(), turnOff.object<jstring>(), turnOn.object<jstring>(),
        quit.object<jstring>(), static_cast<jboolean>(core_.notificationsEnabled()),
        line.object<jstring>());
}

void AndroidShade::notifyMessage(const QString& accountId, const QString& peer,
    const QString& title, const QString& body, const bool reaction)
{
    const QJniObject account = javaString(accountId);
    const QJniObject fingerprint = javaString(peer);
    const QJniObject heading = javaString(title);
    const QJniObject text = javaString(body);
    QJniObject::callStaticMethod<void>(kShade, "notifyMessage",
        "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Z)V",
        account.object<jstring>(), fingerprint.object<jstring>(), heading.object<jstring>(),
        text.object<jstring>(), static_cast<jboolean>(reaction));
}

void AndroidShade::followRinging()
{
    const bool ringing = !core_.ringingPeer().isEmpty();
    if (core_.callTalking() != talking_) {
        talking_ = core_.callTalking();
        QJniObject::callStaticMethod<void>(kShade, "callTalking", "(Z)V", jboolean(talking_));
    }
    AndroidCallAudio::holdNearEar(talking_ && !core_.speakerOn());
    if (ringing && !callShown_ && !core_.windowActive()) {
        const QJniObject account = javaString(core_.ringingAccount());
        const QJniObject fingerprint = javaString(core_.ringingPeerFingerprint());
        const QJniObject heading = javaString(core_.ringingPeer());
        const QJniObject text = javaString(tr("Incoming audio call"));
        QJniObject::callStaticMethod<void>(kShade, "notifyCall",
            "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)V",
            account.object<jstring>(), fingerprint.object<jstring>(), heading.object<jstring>(),
            text.object<jstring>());
        callShown_ = true;
    } else if (!ringing && callShown_) {
        QJniObject::callStaticMethod<void>(kShade, "cancelCall", "()V");
        callShown_ = false;
    }
}

}  // namespace bazarish::app
