// Bazarish project (c) 2026
#include "DesktopEntry.hpp"

#pragma push_macro("emit")
#undef emit
#include <bazarish/Links.hpp>
#include <bazarish/Log.hpp>
#pragma pop_macro("emit")

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QProcess>
#include <QStandardPaths>
#include <QString>

#include <stdexcept>

namespace bazarish::app {

namespace {

const char* const kEntryName = "bazarish";
const char* const kWmClass = "Bazarish";
constexpr int kInstalledIconSize = 512;

bool writeIfChanged(const QString& path, const QByteArray& content)
{
    QFile existing(path);
    if (existing.exists() && existing.open(QIODevice::ReadOnly)
        && existing.readAll() == content) {
        return false;
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
    // An AppImage is launched through a path of the runtime's making, so it says
    // where it really is; anything else stands where it stands.
    const QByteArray image = qgetenv("APPIMAGE");
    const QString binary = image.isEmpty() ? QCoreApplication::applicationFilePath()
                                           : QString::fromLocal8Bit(image);
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
        "Exec=\"%1\" %u\n"
        "Icon=%2\n"
        "Categories=Network;InstantMessaging;\n"
        "MimeType=x-scheme-handler/%4;\n"
        "Terminal=false\n"
        "StartupWMClass=%3\n")
                              .arg(binary, QLatin1String(kEntryName),
                                  QLatin1String(kWmClass), QLatin1String(bazarish::kUriScheme));
    if (writeIfChanged(entryPath, entry.toUtf8())) {
        bazarish::log::info("desktop entry written to {}", entryPath.toStdString());
        // Until the database is rebuilt the desktop hands bazarish:// to nobody.
        if (!QProcess::startDetached(QStringLiteral("update-desktop-database"), {appsDir})) {
            bazarish::log::warn("update-desktop-database did not run; bazarish:// links"
                                " reach this client after the next login");
        }
    }
}

}  // namespace bazarish::app
