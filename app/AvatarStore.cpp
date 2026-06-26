// Bazarish project (c) 2026
#include "AvatarStore.hpp"

namespace bazarish::app {

AvatarStore& AvatarStore::instance()
{
    static AvatarStore store;
    return store;
}

void AvatarStore::put(const QString& fingerprint, const QByteArray& imageData)
{
    if (fingerprint.isEmpty()) {
        return;
    }
    QImage decoded;
    if (!imageData.isEmpty()) {
        decoded = QImage::fromData(imageData);  // format auto-detected (PNG/JPEG)
    }
    {
        QWriteLocker locker(&lock_);
        if (decoded.isNull()) {
            images_.remove(fingerprint);  // empty/undecodable: fall back to the identicon
        } else {
            images_.insert(fingerprint, decoded);
        }
    }
    // Bump after releasing the lock; revision_ is only touched on the GUI thread.
    ++revision_;
    emit revisionChanged();
}

QImage AvatarStore::image(const QString& fingerprint) const
{
    QReadLocker locker(&lock_);
    return images_.value(fingerprint);  // QImage is implicitly shared: a cheap copy
}

}  // namespace bazarish::app
