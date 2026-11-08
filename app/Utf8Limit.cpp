// Bazarish project (c) 2026
#include "Utf8Limit.hpp"

namespace bazarish::app {

void Utf8Limit::setMaxBytes(const int maxBytes)
{
    if (maxBytes_ == maxBytes) {
        return;
    }
    maxBytes_ = maxBytes;
    emit maxBytesChanged();
    emit changed();
}

QValidator::State Utf8Limit::validate(QString& input, int&) const
{
    return input.toUtf8().size() <= maxBytes_ ? Acceptable : Invalid;
}

}  // namespace bazarish::app
