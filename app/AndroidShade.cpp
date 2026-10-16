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

#include <atomic>
#include <functional>
#include <iterator>
#include <stdexcept>

namespace bazarish::app {
namespace {

const char* const kBridge = "sh/bazari/client/Bazarish";

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
    };
    QJniEnvironment env;
    if (!env.registerNativeMethods(kBridge, natives, static_cast<int>(std::size(natives)))) {
        throw std::runtime_error("the Android bridge did not take its native methods");
    }
    gApp.store(&app);

    show();
    QJniObject::callStaticMethod<void>(kBridge, "askNotifications",
        "(Landroid/content/Context;)V", context().object());

    connect(&notifier, &Notifier::announced, this, &AndroidShade::notifyMessage);
    connect(&app_, &AppController::notificationsEnabledChanged, this, &AndroidShade::show);
    connect(&translations, &Translations::languageChanged, this, &AndroidShade::show);
    connect(&app_, &AppController::ringingChanged, this, &AndroidShade::followRinging);
    connect(qApp, &QGuiApplication::applicationStateChanged, this,
        [](const Qt::ApplicationState state) {
            if (state == Qt::ApplicationActive) {
                QJniObject::callStaticMethod<void>(kBridge, "clearMessages",
                    "(Landroid/content/Context;)V", context().object());
            }
        });
}

AndroidShade::~AndroidShade()
{
    gApp.store(nullptr);
}

bool AndroidShade::backgroundRestricted()
{
    return QJniObject::callStaticMethod<jboolean>(kBridge, "backgroundRestricted",
        "(Landroid/content/Context;)Z", context().object());
}

void AndroidShade::openBackgroundSettings()
{
    QJniObject::callStaticMethod<void>(kBridge, "openBackgroundSettings",
        "(Landroid/content/Context;)V", context().object());
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

void AndroidShade::stop()
{
    QJniObject::callStaticMethod<void>(kBridge, "stop", "(Landroid/content/Context;)V",
        context().object());
}

void AndroidShade::show()
{
    const QJniObject running = javaString(tr("Background work"));
    const QJniObject messages = javaString(tr("Messages"));
    const QJniObject reactions = javaString(tr("Reactions"));
    const QJniObject calls = javaString(tr("Calls"));
    const QJniObject turnOff = javaString(tr("Turn off notifications"));
    const QJniObject turnOn = javaString(tr("Turn on notifications"));
    const QJniObject quit = javaString(tr("Quit"));
    QJniObject::callStaticMethod<void>(kBridge, "show",
        "(Landroid/content/Context;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;"
        "Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Z)V",
        context().object(), running.object<jstring>(), messages.object<jstring>(),
        reactions.object<jstring>(), calls.object<jstring>(), turnOff.object<jstring>(),
        turnOn.object<jstring>(), quit.object<jstring>(),
        static_cast<jboolean>(app_.notificationsEnabled()));
}

void AndroidShade::notifyMessage(const QString& accountId, const QString& peer,
    const QString& title, const QString& body, const bool reaction)
{
    const QJniObject account = javaString(accountId);
    const QJniObject fingerprint = javaString(peer);
    const QJniObject heading = javaString(title);
    const QJniObject text = javaString(body);
    QJniObject::callStaticMethod<void>(kBridge, "notifyMessage",
        "(Landroid/content/Context;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;"
        "Ljava/lang/String;Z)V",
        context().object(), account.object<jstring>(), fingerprint.object<jstring>(),
        heading.object<jstring>(), text.object<jstring>(), static_cast<jboolean>(reaction));
}

void AndroidShade::followRinging()
{
    const bool ringing = !app_.ringingPeer().isEmpty();
    if (ringing && !callShown_ && QGuiApplication::applicationState() != Qt::ApplicationActive) {
        const QJniObject account = javaString(app_.ringingAccount());
        const QJniObject fingerprint = javaString(app_.ringingPeerFingerprint());
        const QJniObject heading = javaString(app_.ringingPeer());
        const QJniObject text = javaString(tr("Incoming audio call"));
        QJniObject::callStaticMethod<void>(kBridge, "notifyCall",
            "(Landroid/content/Context;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;"
            "Ljava/lang/String;)V",
            context().object(), account.object<jstring>(), fingerprint.object<jstring>(),
            heading.object<jstring>(), text.object<jstring>());
        callShown_ = true;
    } else if (!ringing && callShown_) {
        QJniObject::callStaticMethod<void>(kBridge, "cancelCall", "(Landroid/content/Context;)V",
            context().object());
        callShown_ = false;
    }
}

}  // namespace bazarish::app
