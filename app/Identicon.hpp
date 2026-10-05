// Bazarish project (c) 2026
#pragma once

#include <QQuickImageProvider>

namespace bazarish::app {

QImage renderIdenticon(const QString& id, int dim);

class IdenticonProvider : public QQuickImageProvider {
public:
    IdenticonProvider();
    QImage requestImage(const QString& id, QSize* size, const QSize& requestedSize) override;
};

class AvatarProvider : public QQuickImageProvider {
public:
    AvatarProvider();
    QImage requestImage(const QString& id, QSize* size, const QSize& requestedSize) override;
};

class QrImageProvider : public QQuickImageProvider {
public:
    QrImageProvider();
    QImage requestImage(const QString& id, QSize* size, const QSize& requestedSize) override;
};

}  // namespace bazarish::app
