// Bazarish project (c) 2026
#include "LocalOnlyNetwork.hpp"

#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QStringList>

namespace bazarish::app {

namespace {

class LocalOnlyManager : public QNetworkAccessManager {
public:
    using QNetworkAccessManager::QNetworkAccessManager;

protected:
    QNetworkReply* createRequest(
        const Operation op, const QNetworkRequest& request, QIODevice* const outgoing) override
    {
        static const QStringList kLocalSchemes
            = {QStringLiteral("qrc"), QStringLiteral("file"), QStringLiteral("data")};
        if (kLocalSchemes.contains(request.url().scheme())) {
            return QNetworkAccessManager::createRequest(op, request, outgoing);
        }
        // An empty URL fails at once as an unknown protocol, without touching the network.
        return QNetworkAccessManager::createRequest(op, QNetworkRequest(QUrl()), outgoing);
    }
};

}  // namespace

QNetworkAccessManager* LocalOnlyNetwork::create(QObject* const parent)
{
    // ownership: allowed - the engine names the parent that deletes it.
    return new LocalOnlyManager(parent);
}

}  // namespace bazarish::app
