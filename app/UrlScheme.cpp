// Bazarish project (c) 2026
#include "UrlScheme.hpp"

#include <bazarish/Links.hpp>

#include <QCoreApplication>
#include <QDir>
#include <QSettings>
#include <QString>

#include <stdexcept>

namespace bazarish::app {

void ensureUrlScheme()
{
    const QString binary = QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
    QSettings scheme(QStringLiteral("HKEY_CURRENT_USER\\Software\\Classes\\")
            + QString::fromLatin1(kUriScheme),
        QSettings::NativeFormat);
    scheme.setValue(QStringLiteral("Default"), QStringLiteral("URL:Bazarish"));
    // Its presence, not its value, is what marks the key as a protocol.
    scheme.setValue(QStringLiteral("URL Protocol"), QString());
    scheme.setValue(QStringLiteral("DefaultIcon/Default"), binary + QStringLiteral(",0"));
    scheme.setValue(QStringLiteral("shell/open/command/Default"),
        QStringLiteral("\"") + binary + QStringLiteral("\" \"%1\""));
    scheme.sync();
    if (scheme.status() != QSettings::NoError) {
        throw std::runtime_error("the bazarish:// scheme was not registered");
    }
}

}  // namespace bazarish::app
