// Bazarish project (c) 2026
#include "PictureStore.hpp"

#include <QBuffer>
#include <QImageReader>

#include <algorithm>
#include <vector>

namespace bazarish::app {

namespace {

constexpr qint64 kDecodedBudgetBytes = 64 * 1024 * 1024;
constexpr qint64 kStoredBudgetBytes = 32 * 1024 * 1024;
constexpr int kBytesPerPixel = 4;

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

bool PictureStore::put(const QString& e2eId, const QByteArray& data)
{
    if (!looksLikeAPicture(data)) {
        return false;
    }
    {
        const QWriteLocker locker(&lock_);
        const auto existing = entries_.constFind(e2eId);
        if (existing != entries_.constEnd()) {
            return true;
        }
        Entry entry;
        entry.bytes = data;
        entry.usedAt = ++clock_;
        storedBytes_ += data.size();
        entries_.insert(e2eId, entry);
        evictLocked();
        ++revision_;
    }
    emit revisionChanged();
    return true;
}

bool PictureStore::has(const QString& e2eId) const
{
    const QReadLocker locker(&lock_);
    return entries_.contains(e2eId);
}

QImage PictureStore::image(const QString& e2eId)
{
    {
        const QReadLocker locker(&lock_);
        const auto found = entries_.constFind(e2eId);
        if (found == entries_.constEnd()) {
            return {};
        }
        if (!found->decoded.isNull()) {
            return found->decoded;
        }
    }

    QByteArray data;
    {
        const QReadLocker locker(&lock_);
        const auto found = entries_.constFind(e2eId);
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
    const auto found = entries_.find(e2eId);
    if (found == entries_.end()) {
        return decoded;
    }
    if (found->decoded.isNull()) {
        found->decoded = decoded;
        decodedBytes_ += decodedSize(decoded);
    }
    found->usedAt = ++clock_;
    evictLocked();
    return decoded;
}

QByteArray PictureStore::bytes(const QString& e2eId) const
{
    const QReadLocker locker(&lock_);
    const auto found = entries_.constFind(e2eId);
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
    const QString e2eId = id.section(QLatin1Char('?'), 0, 0);
    const QImage image = PictureStore::instance().image(e2eId);
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
