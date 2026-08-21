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

// The pictures of the conversations that have been on screen, keyed by the
// message that carries them. The bytes come out of the profile database - the
// only place they are kept - and are decoded when something actually draws them,
// on the thread that draws.
//
// It holds a bounded amount: a chat scrolled through for an hour must not turn
// into a heap full of bitmaps, so the least recently drawn are dropped once the
// budget is passed. Dropping costs nothing but a re-decode of bytes that are
// still there.
//
// Thread-safe: filled from the GUI thread, decoded and read from the QML
// image-loading thread. A revision that bumps on every change lets an Image whose
// source embeds it reload when its picture arrives.
class PictureStore : public QObject {
    Q_OBJECT
    Q_PROPERTY(int revision READ revision NOTIFY revisionChanged)
public:
    static PictureStore& instance();

    int revision() const { return revision_; }

    // Keeps a picture's bytes. Returns false when they are not a picture at all -
    // the message that carried them is broken, and says so.
    bool put(const QString& messageId, const QByteArray& bytes);

    // Whether this message's bytes are here.
    bool has(const QString& messageId) const;

    // The picture, decoded on first use and kept while it fits in the budget.
    QImage image(const QString& messageId);

    // The bytes as they were stored, for writing the picture out or copying it.
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

    struct Entry {
        QByteArray bytes;
        QImage decoded;      // empty until something draws it
        quint64 usedAt = 0;  // for dropping the least recently drawn first
    };

    // Caller must hold the write lock.
    void evictLocked();

    mutable QReadWriteLock lock_;
    QHash<QString, Entry> entries_;
    quint64 clock_ = 0;
    qint64 decodedBytes_ = 0;
    qint64 storedBytes_ = 0;
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
