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
        decoded = QImage::fromData(imageData);
    }
    {
        QWriteLocker locker(&lock_);
        if (decoded.isNull()) {
            images_.remove(fingerprint);
        } else {
            images_.insert(fingerprint, decoded);
        }
    }
    ++revision_;
    emit revisionChanged();
}

QImage AvatarStore::image(const QString& fingerprint) const
{
    QReadLocker locker(&lock_);
    return images_.value(fingerprint);
}

}  // namespace bazarish::app
