// Bazarish project (c) 2026
#include "Documents.hpp"
#include "TestUtil.hpp"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <cstdio>
#include <cstdlib>

namespace {

QByteArray readAll(const QString& path)
{
    QFile file(path);
    CHECK(file.open(QIODevice::ReadOnly));
    return file.readAll();
}

}  // namespace

int main()
{
    const QTemporaryDir root;
    CHECK(root.isValid());
    const QDir downloads(root.filePath(QStringLiteral("Загрузки")));

    const QByteArray first("first picture");
    const QByteArray second("second picture");
    bazarish::app::saveIntoFolder(downloads, QStringLiteral("Снимок.jpg"), first);
    bazarish::app::saveIntoFolder(downloads, QStringLiteral("Снимок.jpg"), second);

    CHECK(readAll(downloads.filePath(QStringLiteral("Снимок.jpg"))) == first);
    CHECK(readAll(downloads.filePath(QStringLiteral("Снимок (1).jpg"))) == second);
    std::fprintf(stderr, "TestDownloads passed\n");
    return 0;
}
