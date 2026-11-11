// Bazarish project (c) 2026
#include "LocalOnlyNetwork.hpp"
#include "TestUtil.hpp"

#include <QCoreApplication>
#include <QEventLoop>
#include <QFile>
#include <QHostAddress>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QTcpServer>
#include <QTemporaryDir>
#include <QTimer>

#include <cstdio>
#include <memory>

using bazarish::app::LocalOnlyNetwork;

namespace {

constexpr int kSettleMs = 500;

QNetworkReply::NetworkError fetched(QNetworkAccessManager& manager, const QUrl& url)
{
    QNetworkReply* const reply = manager.get(QNetworkRequest(url));
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    if (!reply->isFinished()) {
        loop.exec();
    }
    const QNetworkReply::NetworkError error = reply->error();
    reply->deleteLater();
    return error;
}

}  // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    LocalOnlyNetwork factory;
    const std::unique_ptr<QNetworkAccessManager> manager(factory.create(nullptr));

    QTcpServer listener;
    CHECK(listener.listen(QHostAddress::LocalHost));
    int dialled = 0;
    QObject::connect(&listener, &QTcpServer::newConnection, [&]() { ++dialled; });
    const QUrl remote(QStringLiteral("http://127.0.0.1:%1/x").arg(listener.serverPort()));
    CHECK(fetched(*manager, remote) != QNetworkReply::NoError);
    QEventLoop settle;
    QTimer::singleShot(kSettleMs, &settle, &QEventLoop::quit);
    settle.exec();
    CHECK(dialled == 0);
    CHECK(!listener.hasPendingConnections());

    QTemporaryDir dir;
    CHECK(dir.isValid());
    QFile file(dir.filePath(QStringLiteral("page.txt")));
    CHECK(file.open(QIODevice::WriteOnly));
    const QByteArray content = QByteArrayLiteral("local");
    CHECK(file.write(content) == content.size());
    file.close();
    CHECK(fetched(*manager, QUrl::fromLocalFile(file.fileName())) == QNetworkReply::NoError);

    const std::unique_ptr<QNetworkReply> picked(
        manager->get(QNetworkRequest(QUrl(QStringLiteral("content://media/picker/0/1")))));
    CHECK(picked->url().scheme() == QStringLiteral("content"));
    const std::unique_ptr<QNetworkReply> refused(manager->get(QNetworkRequest(remote)));
    CHECK(refused->url().isEmpty());

    std::printf("TestLocalOnlyNetwork: ok\n");
    return 0;
}
