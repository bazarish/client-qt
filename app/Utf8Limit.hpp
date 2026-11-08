// Bazarish project (c) 2026
#pragma once

#include <QValidator>
#include <QtQmlIntegration>

namespace bazarish::app {

// Caps a field in UTF-8 bytes, the unit the core limits names and greetings in.
class Utf8Limit : public QValidator {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(int maxBytes READ maxBytes WRITE setMaxBytes NOTIFY maxBytesChanged)

public:
    using QValidator::QValidator;

    int maxBytes() const { return maxBytes_; }
    void setMaxBytes(int maxBytes);
    State validate(QString& input, int& position) const override;

signals:
    void maxBytesChanged();

private:
    int maxBytes_ = 0;
};

}  // namespace bazarish::app
