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

    static constexpr int kMaxSide = 1024;

    void put(const QString& fingerprint, const QByteArray& imageData);

    bool has(const QString& fingerprint) const;
    QImage image(const QString& fingerprint);

signals:
    void revisionChanged();

private:
    explicit AvatarStore(QObject* parent = nullptr)
        : QObject(parent)
    {
    }

    mutable QReadWriteLock lock_;
    QHash<QString, QByteArray> bytes_;
    QHash<QString, QImage> images_;
    int revision_ = 0;
};

}  // namespace bazarish::app
