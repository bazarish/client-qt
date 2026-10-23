// Bazarish project (c) 2026
#pragma once

#include <QHash>
#include <QObject>
#include <QSet>
#include <QString>
#include <QVariantMap>

namespace bazarish::app {

class ContactBook;
class TranscriptStore;

class MessageIngest : public QObject {
    Q_OBJECT
public:
    MessageIngest(TranscriptStore& store, const ContactBook& contacts, QObject* parent = nullptr);

    [[nodiscard]] bool takeIn(const QVariantMap& message);

signals:
    void rowAdded(const QString& peer, qint64 id, bool own);
    void rowChanged(const QString& peer, qint64 id);
    void rowRemoved(const QString& peer, qint64 id);
    void peerReset(const QString& peer);
    void peerForgotten(const QString& peer);
    void chatTimerChanged(const QString& peer, qint64 seconds);
    void botMenuChanged(const QString& peer);
    void chatChanged(const QString& peer);
    void chatsChanged();
    void deliveredThrough(const QString& peer, qint64 uptoId);
    void reactionsChanged();
    void groupChanged(const QString& peer);
    void accountRenamed(const QString& name);
    void contactsRevised();
    void settingsAsked();
    void readElsewhere(const QString& peer);
    void editArrived(const QString& peer, qint64 localId, const QString& ref);
    void messageArrived(const QString& peer);
    void reactionArrived(const QString& peer, const QString& target, const QString& emoji);

private:
    bool noteGroupBlock(const QString& peer, const QVariantMap& message);
    bool applyGroupReactions(const QString& peer, const QString& json);
    bool markOutgoingRead(const QString& peer, qint64 uptoId);
    qint64 orderFloor(const QString& peer, const QVariantMap& message) const;

    TranscriptStore& store_;
    const ContactBook& contacts_;
    QHash<QString, QSet<QString>> receiptsAhead_;
};

}  // namespace bazarish::app
