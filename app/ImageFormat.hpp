// Bazarish project (c) 2026
#pragma once

#include <QByteArray>

namespace bazarish::app {

// "png", "jpeg", or empty for anything else.
QByteArray pictureFormat(const QByteArray& bytes);

}  // namespace bazarish::app
