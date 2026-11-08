// Bazarish project (c) 2026
#include "MessageIngest.hpp"

#include "ContactBook.hpp"
#include "SessionShared.hpp"
#include "SystemNotes.hpp"
#include "TranscriptStore.hpp"

#include <QByteArray>

namespace bazarish::app {

namespace {

QString qt(const std::string& text)
{
    return QString::fromStdString(text);
}

}  // namespace

class MessageIngest::Relay : public client::IngestNotes, public client::IngestListener {
public:
    Relay(MessageIngest& owner, const ContactBook& contacts)
        : owner_(owner)
        , contacts_(contacts)
    {
    }

    std::string chatCleared(const bool ours, const std::string& peer) const override
    {
        return chatClearedNote(ours, contacts_.peerName(qt(peer))).toStdString();
    }

    std::string chatTimer(
        const std::int64_t seconds, const bool ours, const std::string& peer) const override
    {
        return chatTimerNote(seconds, ours, contacts_.peerName(qt(peer))).toStdString();
    }

    std::string contactAccepted(const std::string& peer) const override
    {
        return encodeSystemNote(
            QT_TR_NOOP("%1 accepted your contact request"), {contacts_.peerName(qt(peer))})
            .toStdString();
    }

    std::string unsupported(const std::string& rawType, const Bytes& rawBody) const override
    {
        const QByteArray body = toQByteArray(rawBody);
        return encodeUnsupportedMessage(qt(rawType), QString::fromLatin1(body.toBase64()))
            .toStdString();
    }

    void rowAdded(const std::string& peer, const std::int64_t id, const bool own) override
    {
        emit owner_.rowAdded(qt(peer), id, own);
    }
    void rowChanged(const std::string& peer, const std::int64_t id) override
    {
        emit owner_.rowChanged(qt(peer), id);
    }
    void rowRemoved(const std::string& peer, const std::int64_t id, const std::string&) override
    {
        emit owner_.rowRemoved(qt(peer), id);
    }
    void peerReset(const std::string& peer) override { emit owner_.peerReset(qt(peer)); }
    void peerForgotten(const std::string& peer) override { emit owner_.peerForgotten(qt(peer)); }
    void chatTimerChanged(const std::string& peer, const std::int64_t seconds) override
    {
        emit owner_.chatTimerChanged(qt(peer), seconds);
    }
    void botMenuChanged(const std::string& peer) override { emit owner_.botMenuChanged(qt(peer)); }
    void chatsChanged() override { emit owner_.chatsChanged(); }
    void deliveredThrough(const std::string& peer, const std::int64_t uptoId) override
    {
        emit owner_.deliveredThrough(qt(peer), uptoId);
    }
    void reactionSet(const std::string& peer, const std::string& target,
        const std::string& reactor, const std::string& emoji) override
    {
        if (reactor != contacts_.ownFingerprint().toStdString() && !emoji.empty()) {
            emit owner_.reactionArrived(qt(peer), qt(target), qt(emoji));
        }
        emit owner_.reactionsChanged();
    }
    void groupReactionsChanged(const std::string&) override { emit owner_.reactionsChanged(); }
    void groupChanged(const std::string& peer) override { emit owner_.groupChanged(qt(peer)); }
    void accountRenamed(const std::string& name) override { emit owner_.accountRenamed(qt(name)); }
    void contactsRevised() override { emit owner_.contactsRevised(); }
    void settingsAsked() override { emit owner_.settingsAsked(); }
    void readElsewhere(const std::string& peer, const std::int64_t throughMs) override
    {
        emit owner_.readElsewhere(qt(peer), throughMs);
    }
    void editArrived(
        const std::string& peer, const std::int64_t localId, const std::string& ref) override
    {
        emit owner_.editArrived(qt(peer), localId, qt(ref));
    }
    void messageArrived(const std::string& peer) override { emit owner_.messageArrived(qt(peer)); }

private:
    MessageIngest& owner_;
    const ContactBook& contacts_;
};

MessageIngest::MessageIngest(
    TranscriptStore& store, const ContactBook& contacts, QObject* const parent)
    : QObject(parent)
    , relay_(std::make_unique<Relay>(*this, contacts))
    , ingest_(store.core(), [&contacts]() { return contacts.ownFingerprint().toStdString(); },
          *relay_, *relay_)
{
}

MessageIngest::~MessageIngest() = default;

bool MessageIngest::takeIn(const client::IncomingMessage& message)
{
    return ingest_.takeIn(message);
}

}  // namespace bazarish::app
