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
// The orientation a camera wrote into the file is applied: the re-encoded bytes carry none.
PreparedPicture preparePictureFile(const QString& location);

}  // namespace bazarish::app
