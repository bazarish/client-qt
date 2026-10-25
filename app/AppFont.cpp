// Bazarish project (c) 2026
#include "AppFont.hpp"

#include <bazarish/Log.hpp>

#include <QFont>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QStringList>

namespace bazarish::app {

namespace {

constexpr int kBasePixels = 14;

}  // namespace

void installAppFont()
{
    for (const char* const font :
        {":/fonts/RobotoMono-Regular.ttf", ":/fonts/RobotoMono-Bold.ttf"}) {
        if (QFontDatabase::addApplicationFont(QString::fromLatin1(font)) < 0) {
            bazarish::log::warn("brand font not loaded: {}", font);
        }
    }
    if (QFontDatabase::addApplicationFont(":/fonts/TwemojiMozilla.ttf") < 0) {
        bazarish::log::warn("emoji font not loaded: emoji are drawn by the system's fonts");
    }
    QFont base;
    base.setFamilies({QStringLiteral("Roboto Mono"), QStringLiteral("Twemoji Mozilla")});
    base.setStyleHint(QFont::Monospace);
    base.setPixelSize(kBasePixels);
    QGuiApplication::setFont(base);
}

}  // namespace bazarish::app
