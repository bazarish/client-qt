// Bazarish project (c) 2026
#include "DesktopEntry.hpp"

#pragma push_macro("emit")
#undef emit
#include <bazarish/Log.hpp>
#pragma pop_macro("emit")

#include <QDir>
#include <QFile>
#include <QImage>
#include <QStandardPaths>
#include <QString>

#include <stdexcept>

namespace bazarish::app {

namespace {

// The name the window reports itself under (Qt's desktop file name on Wayland,
// the WM class on X11); the entry has to be called the same or nothing matches.
const char* const kEntryName = "bazarish";
const char* const kWmClass = "Bazarish";
// hicolor is where a desktop looks for an icon named in an entry, and this is the
// size the master is.
constexpr int kInstalledIconSize = 512;

bool writeIfChanged(const QString& path, const QByteArray& content)
{
    QFile existing(path);
    if (existing.exists() && existing.open(QIODevice::ReadOnly)
        && existing.readAll() == content) {
        return false;  // leaving it alone keeps its timestamp, and the caches with it
    }
    existing.close();
    QFile out(path);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        throw std::runtime_error(
            "desktop entry: cannot write " + path.toStdString() + ": " + out.errorString().toStdString());
    }
    out.write(content);
    return true;
}

}  // namespace

void ensureDesktopEntry()
{
    const QByteArray image = qgetenv("APPIMAGE");
    if (image.isEmpty()) {
        return;  // not an AppImage: whoever installed this owns its entry
    }
    const QString dataDir = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    if (dataDir.isEmpty()) {
        return;
    }
    const QString appsDir = dataDir + QStringLiteral("/applications");
    const QString iconDir = dataDir + QStringLiteral("/icons/hicolor/%1x%1/apps")
                                          .arg(kInstalledIconSize);
    if (!QDir().mkpath(appsDir) || !QDir().mkpath(iconDir)) {
        throw std::runtime_error("desktop entry: cannot create " + appsDir.toStdString());
    }

    const QString iconPath
        = iconDir + QStringLiteral("/") + QLatin1String(kEntryName) + QStringLiteral(".png");
    if (!QFile::exists(iconPath)) {
        const QImage master(QStringLiteral(":/icon/bazarish.png"));
        if (!master.save(iconPath, "PNG")) {
            throw std::runtime_error("desktop entry: cannot write " + iconPath.toStdString());
        }
    }

    const QString entryPath
        = appsDir + QStringLiteral("/") + QLatin1String(kEntryName) + QStringLiteral(".desktop");
    const QString entry = QStringLiteral(
        "[Desktop Entry]\n"
        "Type=Application\n"
        "Name=Bazarish\n"
        "Comment=I2P messenger\n"
        "Exec=\"%1\"\n"
        "Icon=%2\n"
        "Categories=Network;InstantMessaging;\n"
        "Terminal=false\n"
        "StartupWMClass=%3\n")
                              .arg(QString::fromLocal8Bit(image), QLatin1String(kEntryName),
                                  QLatin1String(kWmClass));
    if (writeIfChanged(entryPath, entry.toUtf8())) {
        bazarish::log::info("desktop entry written to {}", entryPath.toStdString());
    }
}

}  // namespace bazarish::app
