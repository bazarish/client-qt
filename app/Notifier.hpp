// Bazarish project (c) 2026
#pragma once

#ifndef Q_OS_ANDROID
#include "NotifySound.hpp"
#endif

#include <QElapsedTimer>
#include <QObject>
#include <QString>

namespace bazarish::app {

class CoreHost;

class Notifier : public QObject {
    Q_OBJECT
public:
    explicit Notifier(CoreHost& core, QObject* parent = nullptr);

signals:
    void announced(const QString& accountId, const QString& peer, const QString& title,
        const QString& body, bool reaction);

private:
    void announce(const QString& accountId, const QString& peer, const QString& title,
        const QString& body, bool reaction);

    CoreHost& core_;
#ifndef Q_OS_ANDROID
    void play(NotifySound& sound, QElapsedTimer& since);
    void noteUnread();

    NotifySound sound_;
    NotifySound reactionSound_;
    QElapsedTimer sinceSound_;
    QElapsedTimer sinceReactionSound_;
    bool announcedWasRead_ = true;
#endif
};

}  // namespace bazarish::app
