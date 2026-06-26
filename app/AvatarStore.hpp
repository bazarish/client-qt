// Bazarish project (c) 2026
#pragma once

#include <QByteArray>
#include <QHash>
#include <QImage>
#include <QObject>
#include <QReadWriteLock>
#include <QString>

namespace bazarish::app {

// A process-wide registry of real (user-set) avatars, keyed by identity
// fingerprint, feeding the image://avatar provider. Identities are globally
// unique, so one store serves every open account. Thread-safe: written from the
// GUI thread (as avatars load or arrive), read from the QML image-loading
// thread. A bump-on-write revision lets QML bust its image cache: an Image whose
// source embeds the revision reloads whenever any avatar changes.
class AvatarStore : public QObject {
    Q_OBJECT
    Q_PROPERTY(int revision READ revision NOTIFY revisionChanged)
public:
    static AvatarStore& instance();

    int revision() const { return revision_; }

    // Sets (or, with empty data, clears) the decoded avatar for a fingerprint and
    // bumps the revision so bound Image sources reload. Call on the GUI thread.
    void put(const QString& fingerprint, const QByteArray& imageData);

    // The decoded avatar for a fingerprint, or a null image when none is stored.
    // Safe to call from the image-loading thread.
    QImage image(const QString& fingerprint) const;

signals:
    void revisionChanged();

private:
    explicit AvatarStore(QObject* parent = nullptr)
        : QObject(parent)
    {
    }

    mutable QReadWriteLock lock_;
    QHash<QString, QImage> images_;
    int revision_ = 0;
};

}  // namespace bazarish::app
