// Bazarish project (c) 2026
#pragma once

#include "NotifySound.hpp"

#include <QElapsedTimer>
#include <QObject>
#include <QString>

namespace bazarish::app {

class AppController;

class Notifier : public QObject {
    Q_OBJECT
public:
    explicit Notifier(AppController& app, QObject* parent = nullptr);

signals:
    void announced(const QString& accountId, const QString& peer, const QString& title,
        const QString& body, bool reaction);

private:
    void announce(const QString& accountId, const QString& peer, const QString& title,
        const QString& body, bool reaction, NotifySound& sound, QElapsedTimer& since);
    void noteUnread();

    AppController& app_;
    NotifySound sound_;
    NotifySound reactionSound_;
    QElapsedTimer sinceSound_;
    QElapsedTimer sinceReactionSound_;
    bool announcedWasRead_ = true;
};

}  // namespace bazarish::app
