// Bazarish project (c) 2026
#include "Notifier.hpp"

#include "CoreHost.hpp"

namespace bazarish::app {

#ifndef Q_OS_ANDROID
namespace {

constexpr int kSoundSpacingNumerator = 3;
constexpr int kSoundSpacingDenominator = 2;
constexpr int kAssumedSoundMs = 1000;

}  // namespace
#endif

Notifier::Notifier(CoreHost& core, QObject* const parent)
    : QObject(parent)
    , core_(core)
#ifndef Q_OS_ANDROID
    , sound_(CoreHost::soundFolder(), QStringLiteral("notify.wav"))
    , reactionSound_(CoreHost::soundFolder(), QStringLiteral("reaction.wav"))
#endif
{
    connect(&core_, &CoreHost::notificationRequested, this,
        [this](const QString& accountId, const QString& peer, const QString& title,
            const QString& body) { announce(accountId, peer, title, body, false); });
    connect(&core_, &CoreHost::reactionNotificationRequested, this,
        [this](const QString& accountId, const QString& peer, const QString& title,
            const QString& body) { announce(accountId, peer, title, body, true); });
#ifndef Q_OS_ANDROID
    connect(&core_, &CoreHost::accountsChanged, this, &Notifier::noteUnread);
#endif
}

void Notifier::announce(const QString& accountId, const QString& peer, const QString& title,
    const QString& body, const bool reaction)
{
    if (!core_.notificationsEnabled() || !core_.ringingPeer().isEmpty()) {
        return;
    }
    if (core_.windowActive()) {
        if (!reaction) {
            emit arrivedInSight();
        }
        return;
    }
    emit announced(accountId, peer, title, body, reaction);
#ifndef Q_OS_ANDROID
    // On Android the notification channel sounds, under the phone's silent and do-not-disturb.
    if (reaction) {
        play(reactionSound_, sinceReactionSound_);
    } else {
        play(sound_, sinceSound_);
    }
#endif
}

#ifndef Q_OS_ANDROID
void Notifier::noteUnread()
{
    for (const AccountRow& account : core_.accountStatuses()) {
        if (account.unread > 0) {
            return;
        }
    }
    announcedWasRead_ = true;
}

void Notifier::play(NotifySound& sound, QElapsedTimer& since)
{
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
#endif

}  // namespace bazarish::app
