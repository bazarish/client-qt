// Bazarish project (c) 2026
#pragma once

#include <QQuickImageProvider>

namespace bazarish::app {

// Renders a deterministic, symmetric "identicon" avatar from a fingerprint
// string: image://identicon/<fingerprint>. Same fingerprint always yields the
// same colourful pattern, so contacts are visually recognizable.
class IdenticonProvider : public QQuickImageProvider {
public:
    IdenticonProvider();
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
