// Bazarish project (c) 2026
#pragma once

#include <QByteArray>
#include <QHash>
#include <QImage>
#include <QObject>
#include <QReadWriteLock>
#include <QString>

namespace bazarish::app {

class AvatarStore : public QObject {
    Q_OBJECT
    Q_PROPERTY(int revision READ revision NOTIFY revisionChanged)
public:
    static AvatarStore& instance();

    int revision() const { return revision_; }

    void put(const QString& fingerprint, const QByteArray& imageData);

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
