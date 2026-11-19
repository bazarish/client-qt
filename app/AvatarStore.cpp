// Bazarish project (c) 2026
#include "AvatarStore.hpp"

#include "ImageFormat.hpp"

#include <QBuffer>
#include <QImageReader>

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
    {
        QWriteLocker locker(&lock_);
        images_.remove(fingerprint);
        if (pictureFormat(imageData).isEmpty()) {
            bytes_.remove(fingerprint);
        } else {
            bytes_.insert(fingerprint, imageData);
        }
    }
    ++revision_;
    emit revisionChanged();
}

bool AvatarStore::has(const QString& fingerprint) const
{
    QReadLocker locker(&lock_);
    return bytes_.contains(fingerprint);
}

QImage AvatarStore::decode(QByteArray data)
{
    if (data.isEmpty()) {
        return {};
    }
    QBuffer buffer(&data);
    buffer.open(QIODevice::ReadOnly);
    QImageReader reader(&buffer, pictureFormat(data));
    const QSize size = reader.size();
    if (!size.isValid() || size.width() > kMaxSide || size.height() > kMaxSide) {
        return {};
    }
    return reader.read();
}

QImage AvatarStore::image(const QString& fingerprint)
{
    QByteArray data;
    {
        QReadLocker locker(&lock_);
        if (const auto decoded = images_.constFind(fingerprint); decoded != images_.constEnd()) {
            return *decoded;
        }
        data = bytes_.value(fingerprint);
    }
    const QImage decoded = decode(data);
    if (decoded.isNull()) {
        return {};
    }
    QWriteLocker locker(&lock_);
    if (bytes_.value(fingerprint) == data) {
        images_.insert(fingerprint, decoded);
    }
    return decoded;
}

}  // namespace bazarish::app
