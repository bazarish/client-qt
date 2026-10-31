// Bazarish project (c) 2026
#include "SessionController.hpp"

#include "AvatarStore.hpp"

#include <QByteArray>

namespace bazarish::app {

void SessionController::refreshChatRow(const QString& peer)
{
    contacts_.touch(peer, peerName(peer), chatPreview(peer), store_.lastTime(peer));
    contacts_.setUnread(peer, store_.unreadCount(peer));
}

void SessionController::onRowAdded(const QString& peer, const qint64 id, const bool own)
{
    const StoredMessage row = store_.messageById(id);
    if (row.id == 0) {
        return;
    }
    showInActiveView(row, own);
    if (peer == activePeer_) {
        requestPicturesFor({row});
    }
    refreshChatRow(peer);
}

void SessionController::onRowChanged(const QString& peer, const qint64 id)
{
    if (peer == activePeer_) {
        conversation_.replaceById(store_.messageById(id));
    }
    refreshChatRow(peer);
}

void SessionController::onRowRemoved(const QString& peer, const qint64 id)
{
    if (peer == activePeer_) {
        conversation_.removeById(id);
    }
    refreshChatRow(peer);
}

void SessionController::onPeerReset(const QString& peer)
{
    if (peer == activePeer_) {
        loadLatestWindow();
    }
    refreshChatRow(peer);
}

void SessionController::onPeerForgotten(const QString& peer)
{
    contacts_.remove(peer);
    if (peer == activePeer_) {
        openConversation({});
    }
    rebuildChatList();
}

void SessionController::onDeliveredThrough(const QString& peer, const qint64 uptoId)
{
    if (peer != activePeer_) {
        return;
    }
    conversation_.markDeliveredThrough(uptoId);
}

void SessionController::onAvatarReady(const QString& fingerprint, const QByteArray& data)
{
    AvatarStore::instance().put(fingerprint, data);
    if (fingerprint == fingerprint_) {
        avatarBusy_ = false;
        emit avatarChanged();
    }
}

}  // namespace bazarish::app
