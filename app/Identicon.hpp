// Bazarish project (c) 2026
#pragma once

#include <QQuickImageProvider>

namespace bazarish::app {

// Draws the deterministic, symmetric identicon for an id at the given pixel
// size. Same id always yields the same colourful pattern. Shared by the
// identicon provider and the avatar provider's fallback.
QImage renderIdenticon(const QString& id, int dim);

// Renders a deterministic, symmetric "identicon" avatar from a fingerprint
// string: image://identicon/<fingerprint>. Same fingerprint always yields the
// same colourful pattern, so contacts are visually recognizable.
class IdenticonProvider : public QQuickImageProvider {
public:
    IdenticonProvider();
    QImage requestImage(const QString& id, QSize* size, const QSize& requestedSize) override;
};

// Renders a contact's real avatar from the shared AvatarStore, keyed by
// fingerprint: image://avatar/<fingerprint>[?r=<revision>]. The optional query
// only busts the QML image cache when an avatar changes; it is stripped before
// the lookup. Falls back to the identicon when no avatar is stored, so every
// caller can use a single source and get a real photo the moment one arrives.
class AvatarProvider : public QQuickImageProvider {
public:
    AvatarProvider();
    QImage requestImage(const QString& id, QSize* size, const QSize& requestedSize) override;
};

// Renders a single QR symbol from the requested text: image://qr/<text>.
// Returns a blank image when the text is too large for one symbol (the UI
// falls back to showing the link).
class QrImageProvider : public QQuickImageProvider {
public:
    QrImageProvider();
    QImage requestImage(const QString& id, QSize* size, const QSize& requestedSize) override;
};

}  // namespace bazarish::app
