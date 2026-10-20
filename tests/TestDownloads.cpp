// Bazarish project (c) 2026
#include "Documents.hpp"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <cstdio>
#include <cstdlib>

#define CHECK(condition)                                                            \
    do {                                                                            \
        if (!(condition)) {                                                         \
            std::fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__, \
                #condition);                                                        \
            std::exit(1);                                                           \
        }                                                                           \
    } while (false)

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
