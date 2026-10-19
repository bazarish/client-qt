// Bazarish project (c) 2026
#include "AndroidWindow.hpp"

#include "AppController.hpp"

#include <QGuiApplication>
#include <QJniEnvironment>
#include <QJniObject>
#include <QtCore/qcoreapplication_platform.h>

#pragma push_macro("emit")
#undef emit
#include <bazarish/Log.hpp>
#pragma pop_macro("emit")

#include <atomic>
#include <functional>
#include <iterator>
#include <stdexcept>

namespace bazarish::app {
namespace {

const char* const kWindow = "sh/bazari/client/Window";

std::atomic<AndroidWindow*> gWindow{nullptr};
std::atomic<AppController*> gApp{nullptr};

// Android calls in on its own thread; the window answers on its own.
template <typename Target>
void inWindowThread(const std::atomic<Target*>& held, std::function<void(Target&)> action)
{
    Target* const target = held.load();
    if (target == nullptr) {
        return;
    }
    if (!QMetaObject::invokeMethod(
            target, [target, action = std::move(action)]() { action(*target); },
            Qt::QueuedConnection)) {
        bazarish::log::warn("android: a call from Android did not reach the window");
    }
}

void conversationOpened(JNIEnv*, jclass, jstring account, jstring peer)
{
    const QString accountId = QJniObject(account).toString();
    const QString fingerprint = QJniObject(peer).toString();
    inWindowThread<AppController>(gApp, [accountId, fingerprint](AppController& app) {
        app.openConversationOf(accountId, fingerprint);
    });
}

void linkOpened(JNIEnv*, jclass, jstring link)
{
    const QString uri = QJniObject(link).toString();
    inWindowThread<AppController>(gApp, [uri](AppController& app) { app.openLink(uri); });
}

void callAnswered(JNIEnv*, jclass)
{
    inWindowThread<AppController>(gApp, [](AppController& app) { app.answerRinging(); });
}

void unlocked(JNIEnv*, jclass)
{
    inWindowThread<AppController>(gApp, [](AppController& app) { emit app.unlocked(); });
}

void keyboardCovers(JNIEnv*, jclass, jint pixels)
{
    inWindowThread<AppController>(gApp, [pixels](AppController& app) {
        app.setKeyboardInset(pixels / qGuiApp->devicePixelRatio());
    });
}

void coreAnswered(JNIEnv*, jclass)
{
    inWindowThread<AndroidWindow>(gWindow, [](AndroidWindow& window) {
        emit window.coreListening();
    });
}

}  // namespace

AndroidWindow::AndroidWindow(AppController& app, QObject* const parent)
    : QObject(parent)
    , app_(app)
{
    const JNINativeMethod natives[] = {
        {"conversationOpened", "(Ljava/lang/String;Ljava/lang/String;)V",
            reinterpret_cast<void*>(conversationOpened)},
        {"callAnswered", "()V", reinterpret_cast<void*>(callAnswered)},
        {"linkOpened", "(Ljava/lang/String;)V", reinterpret_cast<void*>(linkOpened)},
        {"unlocked", "()V", reinterpret_cast<void*>(unlocked)},
        {"coreListening", "()V", reinterpret_cast<void*>(coreAnswered)},
        {"keyboardCovers", "(I)V", reinterpret_cast<void*>(keyboardCovers)},
    };
    QJniEnvironment env;
    if (!env.registerNativeMethods(kWindow, natives, static_cast<int>(std::size(natives)))) {
        throw std::runtime_error("the Android window did not take its native methods");
    }
    gApp.store(&app);
    gWindow.store(this);

    QJniObject::callStaticMethod<void>(kWindow, "askNotifications", "()V");
    QJniObject::callStaticMethod<void>(kWindow, "followKeyboard", "()V");
    takeHeld();

    connect(&app_, &AppController::ringingChanged, this, &AndroidWindow::followCall);
    connect(&app_, &AppController::callLiveChanged, this, &AndroidWindow::followCall);
    connect(qApp, &QGuiApplication::applicationStateChanged, this,
        [](const Qt::ApplicationState state) {
            if (state == Qt::ApplicationActive) {
                QJniObject::callStaticMethod<void>(kWindow, "clearMessages", "()V");
            }
        });
}

AndroidWindow::~AndroidWindow()
{
    gApp.store(nullptr);
    gWindow.store(nullptr);
}

void AndroidWindow::startCore()
{
    QJniObject::callStaticMethod<void>(kWindow, "startCore", "()V");
}

bool AndroidWindow::backgroundRestricted()
{
    return QJniObject::callStaticMethod<jboolean>(kWindow, "backgroundRestricted", "()Z");
}

bool AndroidWindow::fullScreenCallsRestricted()
{
    return QJniObject::callStaticMethod<jboolean>(kWindow, "fullScreenCallsRestricted", "()Z");
}

void AndroidWindow::openFullScreenCallsSettings()
{
    QJniObject::callStaticMethod<void>(kWindow, "openFullScreenCallsSettings", "()V");
}

void AndroidWindow::openBackgroundSettings()
{
    QJniObject::callStaticMethod<void>(kWindow, "openBackgroundSettings", "()V");
}

void AndroidWindow::moveToBackground()
{
    QJniObject activity = QNativeInterface::QAndroidApplication::context();
    QNativeInterface::QAndroidApplication::runOnAndroidMainThread([activity]() {
        if (!activity.callMethod<jboolean>("moveTaskToBack", "(Z)Z", jboolean(true))) {
            bazarish::log::warn("android: the task did not move to the background");
        }
    });
}

void AndroidWindow::dismissKeyguard()
{
    QJniObject::callStaticMethod<void>(kWindow, "dismissKeyguard", "()V");
}

void AndroidWindow::paintSystemBars(const bool light, const QColor& color)
{
    QJniObject::callStaticMethod<void>(kWindow, "paintSystemBars", "(ZI)V", jboolean(light),
        static_cast<jint>(color.rgba()));
}

void AndroidWindow::followCall()
{
    const bool overLock = !app_.ringingPeer().isEmpty() || app_.callLive();
    if (overLock_ == overLock) {
        return;
    }
    overLock_ = overLock;
    QJniObject::callStaticMethod<void>(kWindow, "overLockScreen", "(Z)V", jboolean(overLock));
}

void AndroidWindow::takeHeld()
{
    const QJniObject held = QJniObject::callStaticObjectMethod(
        kWindow, "takeHeldConversation", "()[Ljava/lang/String;");
    QJniEnvironment env;
    const auto list = static_cast<jobjectArray>(held.object());
    constexpr jsize kAccountAndPeer = 2;
    if (env->GetArrayLength(list) == kAccountAndPeer) {
        const QString accountId
            = QJniObject::fromLocalRef(env->GetObjectArrayElement(list, 0)).toString();
        const QString peer
            = QJniObject::fromLocalRef(env->GetObjectArrayElement(list, 1)).toString();
        app_.openConversationOf(accountId, peer);
    }
    const QString link = QJniObject::callStaticObjectMethod(
        kWindow, "takeHeldLink", "()Ljava/lang/String;").toString();
    if (!link.isEmpty()) {
        app_.openLink(link);
    }
    if (QJniObject::callStaticMethod<jboolean>(kWindow, "takeHeldAnswer", "()Z")) {
        app_.answerRinging();
    }
}

}  // namespace bazarish::app
