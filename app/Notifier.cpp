// Bazarish project (c) 2026
#include "Notifier.hpp"

#include "AppController.hpp"

#include <QGuiApplication>

namespace bazarish::app {

namespace {

constexpr int kSoundSpacingNumerator = 3;
constexpr int kSoundSpacingDenominator = 2;
constexpr int kAssumedSoundMs = 1000;

}  // namespace

Notifier::Notifier(AppController& app, QObject* const parent)
    : QObject(parent)
    , app_(app)
    , sound_(app.soundFolder(), QStringLiteral("notify.wav"))
    , reactionSound_(app.soundFolder(), QStringLiteral("reaction.wav"))
{
    connect(&app_, &AppController::accountsChanged, this, &Notifier::noteUnread);
    connect(&app_, &AppController::notificationRequested, this,
        [this](const QString& accountId, const QString& peer, const QString& title,
            const QString& body) { announce(accountId, peer, title, body, sound_, sinceSound_); });
    connect(&app_, &AppController::reactionNotificationRequested, this,
        [this](const QString& accountId, const QString& peer, const QString& title,
            const QString& body) {
            announce(accountId, peer, title, body, reactionSound_, sinceReactionSound_);
        });
}

void Notifier::noteUnread()
{
    for (const AccountRow& account : app_.accountStatuses()) {
        if (account.unread > 0) {
            return;
        }
    }
    announcedWasRead_ = true;
}

void Notifier::announce(const QString& accountId, const QString& peer, const QString& title,
    const QString& body, NotifySound& sound, QElapsedTimer& since)
{
    if (!app_.notificationsEnabled()) {
        return;
    }
    if (QGuiApplication::applicationState() == Qt::ApplicationActive) {
        return;
    }
    if (!app_.ringingPeer().isEmpty()) {
        return;
    }
    emit announced(accountId, peer, title, body);
    const qint64 length = sound.durationMs() > 0 ? sound.durationMs() : kAssumedSoundMs;
    const qint64 spacing = length * kSoundSpacingNumerator / kSoundSpacingDenominator;
    const bool quiet = !announcedWasRead_ && since.isValid() && since.elapsed() < spacing;
    if (quiet) {
        return;
    }
    announcedWasRead_ = false;
    since.start();
    sound.play();
}

}  // namespace bazarish::app
