// Bazarish project (c) 2026
#include "Notifier.hpp"

#include "CoreHost.hpp"

namespace bazarish::app {

namespace {

constexpr int kSoundSpacingNumerator = 3;
constexpr int kSoundSpacingDenominator = 2;
constexpr int kAssumedSoundMs = 1000;
#ifdef Q_OS_ANDROID
// The notification channel sounds there, under the phone's own silent and do-not-disturb rules.
constexpr bool kPlaysOwnSound = false;
#else
constexpr bool kPlaysOwnSound = true;
#endif

}  // namespace

Notifier::Notifier(CoreHost& core, QObject* const parent)
    : QObject(parent)
    , core_(core)
    , sound_(CoreHost::soundFolder(), QStringLiteral("notify.wav"))
    , reactionSound_(CoreHost::soundFolder(), QStringLiteral("reaction.wav"))
{
    connect(&core_, &CoreHost::accountsChanged, this, &Notifier::noteUnread);
    connect(&core_, &CoreHost::notificationRequested, this,
        [this](const QString& accountId, const QString& peer, const QString& title,
            const QString& body) {
            announce(accountId, peer, title, body, false, sound_, sinceSound_);
        });
    connect(&core_, &CoreHost::reactionNotificationRequested, this,
        [this](const QString& accountId, const QString& peer, const QString& title,
            const QString& body) {
            announce(accountId, peer, title, body, true, reactionSound_, sinceReactionSound_);
        });
}

void Notifier::noteUnread()
{
    for (const AccountRow& account : core_.accountStatuses()) {
        if (account.unread > 0) {
            return;
        }
    }
    announcedWasRead_ = true;
}

void Notifier::announce(const QString& accountId, const QString& peer, const QString& title,
    const QString& body, const bool reaction, NotifySound& sound, QElapsedTimer& since)
{
    if (!core_.notificationsEnabled() || core_.windowActive()) {
        return;
    }
    if (!core_.ringingPeer().isEmpty()) {
        return;
    }
    emit announced(accountId, peer, title, body, reaction);
    if (!kPlaysOwnSound) {
        return;
    }
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
