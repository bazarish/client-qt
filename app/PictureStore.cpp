// Bazarish project (c) 2026
#include "PictureStore.hpp"

#include <QBuffer>
#include <QImageReader>

#include <algorithm>
#include <vector>

namespace bazarish::app {

namespace {

// What the decoded pictures may take between them. A window's worth of chat is a
// few of them; an afternoon of scrolling is not worth a heap.
constexpr qint64 kDecodedBudgetBytes = 64 * 1024 * 1024;
// And what their bytes may take. These are small by construction (a picture is
// prepared to a quarter of a megabyte), so this is hundreds of them.
constexpr qint64 kStoredBudgetBytes = 32 * 1024 * 1024;
constexpr int kBytesPerPixel = 4;

// The first bytes of the two formats worth drawing. A message that announced a
// picture and carried something else is broken, and this is where that is found
// out - decoding waits until something draws it.
bool looksLikeAPicture(const QByteArray& bytes)
{
    static const QByteArray kPng = QByteArray::fromHex("89504E470D0A1A0A");
    if (bytes.startsWith(kPng)) {
        return true;
    }
    return bytes.size() >= 3 && static_cast<unsigned char>(bytes[0]) == 0xFF
        && static_cast<unsigned char>(bytes[1]) == 0xD8
        && static_cast<unsigned char>(bytes[2]) == 0xFF;
}

qint64 decodedSize(const QImage& image)
{
    return static_cast<qint64>(image.width()) * image.height() * kBytesPerPixel;
}

}  // namespace

PictureStore& PictureStore::instance()
{
    static PictureStore store;
    return store;
}

bool PictureStore::put(const QString& messageId, const QByteArray& data)
{
    if (!looksLikeAPicture(data)) {
        return false;
    }
    {
        const QWriteLocker locker(&lock_);
        const auto existing = entries_.constFind(messageId);
        if (existing != entries_.constEnd()) {
            return true;  // already here, and the bytes cannot have changed
        }
        Entry entry;
        entry.bytes = data;
        entry.usedAt = ++clock_;
        storedBytes_ += data.size();
        entries_.insert(messageId, entry);
        evictLocked();
        ++revision_;
    }
    emit revisionChanged();
    return true;
}

bool PictureStore::has(const QString& messageId) const
{
    const QReadLocker locker(&lock_);
    return entries_.contains(messageId);
}

QImage PictureStore::image(const QString& messageId)
{
    {
        const QReadLocker locker(&lock_);
        const auto found = entries_.constFind(messageId);
        if (found == entries_.constEnd()) {
            return {};
        }
        if (!found->decoded.isNull()) {
            // A read lock cannot bump the clock; the entry is used, which is
            // enough to keep it above the ones nothing has drawn.
            return found->decoded;
        }
    }

    // Decode outside the lock: this runs on the image-loading thread and takes
    // as long as the picture is large.
    QByteArray data;
    {
        const QReadLocker locker(&lock_);
        const auto found = entries_.constFind(messageId);
        if (found == entries_.constEnd()) {
            return {};
        }
        data = found->bytes;
    }
    QBuffer buffer(&data);
    buffer.open(QIODevice::ReadOnly);
    QImageReader reader(&buffer);
    const QImage decoded = reader.read();
    if (decoded.isNull()) {
        return {};
    }

    const QWriteLocker locker(&lock_);
    const auto found = entries_.find(messageId);
    if (found == entries_.end()) {
        return decoded;  // dropped while decoding; the caller still gets it
    }
    if (found->decoded.isNull()) {
        found->decoded = decoded;
        decodedBytes_ += decodedSize(decoded);
    }
    found->usedAt = ++clock_;
    evictLocked();
    return decoded;
}

QByteArray PictureStore::bytes(const QString& messageId) const
{
    const QReadLocker locker(&lock_);
    const auto found = entries_.constFind(messageId);
    return found == entries_.constEnd() ? QByteArray() : found->bytes;
}

void PictureStore::evictLocked()
{
    if (decodedBytes_ <= kDecodedBudgetBytes && storedBytes_ <= kStoredBudgetBytes) {
        return;
    }
    std::vector<std::pair<quint64, QString>> byAge;
    byAge.reserve(entries_.size());
    for (auto it = entries_.constBegin(); it != entries_.constEnd(); ++it) {
        byAge.emplace_back(it->usedAt, it.key());
    }
    std::sort(byAge.begin(), byAge.end());
    for (const auto& [usedAt, key] : byAge) {
        if (decodedBytes_ <= kDecodedBudgetBytes && storedBytes_ <= kStoredBudgetBytes) {
            return;
        }
        const auto found = entries_.find(key);
        if (found == entries_.end()) {
            continue;
        }
        // The picture on the way out costs nothing to bring back: its bytes are
        // in the profile, and this is a cache, not the copy.
        decodedBytes_ -= decodedSize(found->decoded);
        storedBytes_ -= found->bytes.size();
        entries_.erase(found);
    }
}

void PictureStore::clear()
{
    {
        const QWriteLocker locker(&lock_);
        entries_.clear();
        decodedBytes_ = 0;
        storedBytes_ = 0;
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
