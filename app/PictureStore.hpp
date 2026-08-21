// Bazarish project (c) 2026
#pragma once

#include <QByteArray>
#include <QHash>
#include <QImage>
#include <QObject>
#include <QQuickImageProvider>
#include <QReadWriteLock>
#include <QString>

namespace bazarish::app {

// The pictures of the open conversation, decoded and ready to draw, keyed by the
// message that carries them. The bytes themselves live in the profile database -
// encrypted, like everything else this client keeps - and are decoded into here
// only while a chat is on screen.
//
// Thread-safe: filled from the GUI thread as messages load, read from the QML
// image-loading thread. A revision that bumps on every write lets an Image whose
// source embeds it reload when the picture arrives.
class PictureStore : public QObject {
    Q_OBJECT
    Q_PROPERTY(int revision READ revision NOTIFY revisionChanged)
public:
    static PictureStore& instance();

    int revision() const { return revision_; }

    // Decodes and keeps a picture. Bytes that are not a picture are not kept,
    // and the caller is told: a message that announced one and delivered
    // something else is broken, not a file.
    bool put(const QString& messageId, const QByteArray& bytes);

    // Whether this picture is decoded and ready to draw.
    bool has(const QString& messageId) const;

    // The picture for a message, or a null image. Safe from the image thread.
    QImage image(const QString& messageId) const;

    // The bytes as they were stored, for writing the picture out to a file.
    QByteArray bytes(const QString& messageId) const;

    // Drops everything (a profile closing).
    void clear();

signals:
    void revisionChanged();

private:
    explicit PictureStore(QObject* parent = nullptr)
        : QObject(parent)
    {
    }

    mutable QReadWriteLock lock_;
    QHash<QString, QImage> images_;
    QHash<QString, QByteArray> bytes_;
    int revision_ = 0;
};

// Serves image://picture/<messageId>.
class PictureProvider : public QQuickImageProvider {
public:
    PictureProvider()
        : QQuickImageProvider(QQuickImageProvider::Image)
    {
    }

    QImage requestImage(const QString& id, QSize* size, const QSize& requestedSize) override;
};

}  // namespace bazarish::app
