// Bazarish project (c) 2026
#include "PictureStore.hpp"

#include <QBuffer>
#include <QImageReader>

namespace bazarish::app {

PictureStore& PictureStore::instance()
{
    static PictureStore store;
    return store;
}

bool PictureStore::put(const QString& messageId, const QByteArray& data)
{
    // The bytes decide what this is. A reader that refuses them is the answer:
    // nothing is drawn, and the bubble says the message is broken.
    QBuffer buffer;
    buffer.setData(data);
    buffer.open(QIODevice::ReadOnly);
    QImageReader reader(&buffer);
    const QImage image = reader.read();
    if (image.isNull()) {
        return false;
    }
    {
        const QWriteLocker locker(&lock_);
        images_.insert(messageId, image);
        bytes_.insert(messageId, data);
        ++revision_;
    }
    emit revisionChanged();
    return true;
}

bool PictureStore::has(const QString& messageId) const
{
    const QReadLocker locker(&lock_);
    return images_.contains(messageId);
}

QImage PictureStore::image(const QString& messageId) const
{
    const QReadLocker locker(&lock_);
    return images_.value(messageId);
}

QByteArray PictureStore::bytes(const QString& messageId) const
{
    const QReadLocker locker(&lock_);
    return bytes_.value(messageId);
}

void PictureStore::clear()
{
    {
        const QWriteLocker locker(&lock_);
        images_.clear();
        bytes_.clear();
        ++revision_;
    }
    emit revisionChanged();
}

QImage PictureProvider::requestImage(const QString& id, QSize* size, const QSize& requestedSize)
{
    // The id carries a cache-busting revision after "?"; the picture is what is
    // before it.
    const QString messageId = id.section(QLatin1Char('?'), 0, 0);
    const QImage image = PictureStore::instance().image(messageId);
    if (image.isNull()) {
        return image;
    }
    if (size != nullptr) {
        *size = image.size();
    }
    if (requestedSize.isValid() && !requestedSize.isEmpty()) {
        return image.scaled(requestedSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    return image;
}

}  // namespace bazarish::app
