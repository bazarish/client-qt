// Bazarish project (c) 2026
#include "AndroidShade.hpp"

#include "AppController.hpp"
#include "Notifier.hpp"
#include "Translations.hpp"

#include <QGuiApplication>
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

const char* const kBridge = "sh/bazari/client/Bazarish";
const char* const kLogTag = "bazarish";
constexpr int kLogLineBytes = 1024;

std::atomic<AppController*> gApp{nullptr};

QJniObject context()
{
    return QNativeInterface::QAndroidApplication::context();
}

QJniObject javaString(const QString& text)
{
    return QJniObject::fromString(text);
}

// The shade's buttons are pressed on Android's own thread; the app answers on its own.
void inAppThread(std::function<void(AppController&)> action)
{
    AppController* const app = gApp.load();
    if (app == nullptr) {
        return;
    }
    if (!QMetaObject::invokeMethod(app, [app, action = std::move(action)]() { action(*app); },
            Qt::QueuedConnection)) {
        bazarish::log::warn("android: a button pressed in the shade did not reach the app");
    }
}

void notificationsToggled(JNIEnv*, jclass)
{
    inAppThread([](AppController& app) {
        app.setNotificationsEnabled(!app.notificationsEnabled());
    });
}

void quitRequested(JNIEnv*, jclass)
{
    inAppThread([](AppController& app) { app.prepareForExit(); });
}

void callAnswered(JNIEnv*, jclass)
{
    inAppThread([](AppController& app) { app.answerRinging(); });
}

void callDeclined(JNIEnv*, jclass)
{
    inAppThread([](AppController& app) { app.declineRinging(); });
}

void conversationOpened(JNIEnv*, jclass, jstring account, jstring peer)
{
    const QString accountId = QJniObject(account).toString();
    const QString fingerprint = QJniObject(peer).toString();
    inAppThread([accountId, fingerprint](AppController& app) {
        app.openConversationOf(accountId, fingerprint);
    });
}

}  // namespace

AndroidShade::AndroidShade(AppController& app, Notifier& notifier, Translations& translations,
    QObject* const parent)
    : QObject(parent)
    , app_(app)
{
    const JNINativeMethod natives[] = {
        {"notificationsToggled", "()V", reinterpret_cast<void*>(notificationsToggled)},
        {"quitRequested", "()V", reinterpret_cast<void*>(quitRequested)},
        {"conversationOpened", "(Ljava/lang/String;Ljava/lang/String;)V",
            reinterpret_cast<void*>(conversationOpened)},
        {"callAnswered", "()V", reinterpret_cast<void*>(callAnswered)},
        {"callDeclined", "()V", reinterpret_cast<void*>(callDeclined)},
    };
    QJniEnvironment env;
    if (!env.registerNativeMethods(kBridge, natives, static_cast<int>(std::size(natives)))) {
        throw std::runtime_error("the Android bridge did not take its native methods");
    }
    gApp.store(&app);

    line_ = presenceLine();
    show();
    // A call notice left by a process that is gone belongs to a call that is gone.
    QJniObject::callStaticMethod<void>(kBridge, "cancelCall", "()V");
    QJniObject::callStaticMethod<void>(kBridge, "askNotifications",
        "(Landroid/content/Context;)V", context().object());
    openHeldConversation();

    connect(&notifier, &Notifier::announced, this, &AndroidShade::notifyMessage);
    connect(&app_, &AppController::notificationsEnabledChanged, this, &AndroidShade::show);
    connect(&app_, &AppController::accountsChanged, this, [this]() {
        const QString line = presenceLine();
        if (line != line_) {
            line_ = line;
            show();
        }
    });
    connect(&translations, &Translations::languageChanged, this, &AndroidShade::show);
    connect(&app_, &AppController::ringingChanged, this, &AndroidShade::followRinging);
    connect(&app_, &AppController::callLiveChanged, this, &AndroidShade::followRinging);
    connect(qApp, &QGuiApplication::applicationStateChanged, this,
        [](const Qt::ApplicationState state) {
            if (state == Qt::ApplicationActive) {
                QJniObject::callStaticMethod<void>(kBridge, "clearMessages", "()V");
            }
        });
}

AndroidShade::~AndroidShade()
{
    gApp.store(nullptr);
}

void AndroidShade::routeStderrToLogcat()
{
    int ends[2] = {-1, -1};
    if (pipe(ends) != 0 || dup2(ends[1], STDERR_FILENO) < 0) {
        throw std::runtime_error("stderr could not be turned towards logcat");
    }
    close(ends[1]);
    std::setvbuf(stderr, nullptr, _IOLBF, 0);
    std::thread([reading = ends[0]]() {
        FILE* const lines = fdopen(reading, "r");
        char line[kLogLineBytes];
        while (lines != nullptr && std::fgets(line, sizeof(line), lines) != nullptr) {
            __android_log_write(ANDROID_LOG_INFO, kLogTag, line);
        }
    }).detach();
}

bool AndroidShade::windowless()
{
    return !QNativeInterface::QAndroidApplication::isActivityContext();
}

bool AndroidShade::backgroundRestricted()
{
    return QJniObject::callStaticMethod<jboolean>(kBridge, "backgroundRestricted", "()Z");
}

bool AndroidShade::fullScreenCallsRestricted()
{
    return QJniObject::callStaticMethod<jboolean>(kBridge, "fullScreenCallsRestricted", "()Z");
}

void AndroidShade::openFullScreenCallsSettings()
{
    QJniObject::callStaticMethod<void>(kBridge, "openFullScreenCallsSettings", "()V");
}

void AndroidShade::openBackgroundSettings()
{
    QJniObject::callStaticMethod<void>(kBridge, "openBackgroundSettings", "()V");
}

void AndroidShade::moveToBackground()
{
    QJniObject activity = context();
    QNativeInterface::QAndroidApplication::runOnAndroidMainThread([activity]() {
        if (!activity.callMethod<jboolean>("moveTaskToBack", "(Z)Z", jboolean(true))) {
            bazarish::log::warn("android: the task did not move to the background");
        }
    });
}

void AndroidShade::startRinging()
{
    QJniObject::callStaticMethod<void>(kBridge, "startRinging", "()V");
}

void AndroidShade::stopRinging()
{
    QJniObject::callStaticMethod<void>(kBridge, "stopRinging", "()V");
}

void AndroidShade::stop()
{
    QJniObject::callStaticMethod<void>(kBridge, "stop", "()V");
}

QString AndroidShade::presenceLine() const
{
    int unread = 0;
    int open = 0;
    bool connected = true;
    for (const AccountRow& account : app_.accountStatuses()) {
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
    QJniObject::callStaticMethod<void>(kBridge, "show",
        "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;"
        "Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;ZLjava/lang/String;)V",
        presence.object<jstring>(), messages.object<jstring>(), reactions.object<jstring>(),
        calls.object<jstring>(), turnOff.object<jstring>(), turnOn.object<jstring>(),
        quit.object<jstring>(), static_cast<jboolean>(app_.notificationsEnabled()),
        line.object<jstring>());
}

void AndroidShade::notifyMessage(const QString& accountId, const QString& peer,
    const QString& title, const QString& body, const bool reaction)
{
    const QJniObject account = javaString(accountId);
    const QJniObject fingerprint = javaString(peer);
    const QJniObject heading = javaString(title);
    const QJniObject text = javaString(body);
    QJniObject::callStaticMethod<void>(kBridge, "notifyMessage",
        "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Z)V",
        account.object<jstring>(), fingerprint.object<jstring>(), heading.object<jstring>(),
        text.object<jstring>(), static_cast<jboolean>(reaction));
}

void AndroidShade::followRinging()
{
    const bool ringing = !app_.ringingPeer().isEmpty();
    const bool overLock = ringing || app_.callLive();
    if (overLock != overLock_) {
        overLock_ = overLock;
        QJniObject::callStaticMethod<void>(kBridge, "overLockScreen", "(Z)V", jboolean(overLock_));
    }
    if (ringing && !callShown_ && QGuiApplication::applicationState() != Qt::ApplicationActive) {
        const QJniObject account = javaString(app_.ringingAccount());
        const QJniObject fingerprint = javaString(app_.ringingPeerFingerprint());
        const QJniObject heading = javaString(app_.ringingPeer());
        const QJniObject text = javaString(tr("Incoming audio call"));
        QJniObject::callStaticMethod<void>(kBridge, "notifyCall",
            "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)V",
            account.object<jstring>(), fingerprint.object<jstring>(), heading.object<jstring>(),
            text.object<jstring>());
        callShown_ = true;
    } else if (!ringing && callShown_) {
        QJniObject::callStaticMethod<void>(kBridge, "cancelCall", "()V");
        callShown_ = false;
    }
}

void AndroidShade::openHeldConversation()
{
    const QJniObject held = QJniObject::callStaticObjectMethod(
        kBridge, "takeHeldConversation", "()[Ljava/lang/String;");
    QJniEnvironment env;
    const auto list = static_cast<jobjectArray>(held.object());
    constexpr jsize kAccountAndPeer = 2;
    if (env->GetArrayLength(list) != kAccountAndPeer) {
        return;
    }
    const QString accountId
        = QJniObject::fromLocalRef(env->GetObjectArrayElement(list, 0)).toString();
    const QString peer = QJniObject::fromLocalRef(env->GetObjectArrayElement(list, 1)).toString();
    app_.openConversationOf(accountId, peer);
}

}  // namespace bazarish::app
