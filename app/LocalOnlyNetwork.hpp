// Bazarish project (c) 2026
#pragma once

#include <QQmlNetworkAccessManagerFactory>

namespace bazarish::app {

class LocalOnlyNetwork : public QQmlNetworkAccessManagerFactory {
public:
    QNetworkAccessManager* create(QObject* parent) override;
};

}  // namespace bazarish::app
