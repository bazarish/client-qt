// Bazarish project (c) 2026
#pragma once

#include <QByteArray>
#include <QImage>
#include <QString>

namespace bazarish::app {

struct PreparedPicture {
    QByteArray bytes;
    QString name;
    QString mime;

    bool isEmpty() const { return bytes.isEmpty(); }
};

PreparedPicture preparePicture(const QImage& image, const QString& baseName);

}  // namespace bazarish::app
