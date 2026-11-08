// Bazarish project (c) 2026
#pragma once

#include <Session.hpp>
#include <TranscriptIngest.hpp>

#include <QObject>

#include <memory>
#include <QString>

namespace bazarish::app {

class ContactBook;
class TranscriptStore;

class MessageIngest : public QObject {
    Q_OBJECT
public:
    MessageIngest(TranscriptStore& store, const ContactBook& contacts, QObject* parent = nullptr);
    ~MessageIngest() override;

    [[nodiscard]] bool takeIn(const client::IncomingMessage& message);

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
    // throughMs is the send time of the newest message another device read.
    void readElsewhere(const QString& peer, qint64 throughMs);
    void editArrived(const QString& peer, qint64 localId, const QString& ref);
    void messageArrived(const QString& peer);
    void reactionArrived(const QString& peer, const QString& target, const QString& emoji);

private:
    class Relay;

    std::unique_ptr<Relay> relay_;
    client::TranscriptIngest ingest_;
};

}  // namespace bazarish::app
