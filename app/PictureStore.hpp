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

class PictureStore : public QObject {
    Q_OBJECT
    Q_PROPERTY(int revision READ revision NOTIFY revisionChanged)
public:
    static PictureStore& instance();

    int revision() const { return revision_; }

    bool put(const QString& e2eId, const QByteArray& bytes);

    bool has(const QString& e2eId) const;

    QImage image(const QString& e2eId);

    QByteArray bytes(const QString& e2eId) const;

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
        QImage decoded;
        quint64 usedAt = 0;
    };

    void evictLocked();

    mutable QReadWriteLock lock_;
    QHash<QString, Entry> entries_;
    quint64 clock_ = 0;
    qint64 decodedBytes_ = 0;
    qint64 storedBytes_ = 0;
    int revision_ = 0;
};

class PictureProvider : public QQuickImageProvider {
public:
    PictureProvider()
        : QQuickImageProvider(QQuickImageProvider::Image)
    {
    }

    QImage requestImage(const QString& id, QSize* size, const QSize& requestedSize) override;
};

}  // namespace bazarish::app
