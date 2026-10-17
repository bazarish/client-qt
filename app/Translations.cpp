// Bazarish project (c) 2026
#include "Translations.hpp"

#include "AppSettings.hpp"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QStringList>
#ifdef Q_OS_ANDROID
#include <QJniObject>
#endif

#include <stdexcept>

namespace bazarish::app {

namespace {

const QString kCatalog = QStringLiteral(":/i18n/messages.json");
// The wording in the code is English, so that language carries no column.
const QString kSourceLanguage = QStringLiteral("en");
const QChar kDisambiguationMark = QChar(u'|');

QStringList preferredLanguages()
{
#ifdef Q_OS_ANDROID
    // QLocale::system() is fed by Qt's Android platform plugin, which the core service runs without.
    const QJniObject locales = QJniObject::callStaticObjectMethod(
        "android/os/LocaleList", "getDefault", "()Landroid/os/LocaleList;");
    if (!locales.isValid()) {
        throw std::runtime_error("Android gave no list of the user's languages");
    }
    return locales.callObjectMethod("toLanguageTags", "()Ljava/lang/String;")
        .toString()
        .split(QChar(u','));
#else
    return QLocale::system().uiLanguages();
#endif
}

QString keyFor(const char* const sourceText, const char* const disambiguation)
{
    const QString source = QString::fromUtf8(sourceText);
    if (disambiguation == nullptr || *disambiguation == '\0') {
        return source;
    }
    return source + kDisambiguationMark + QString::fromUtf8(disambiguation);
}

}  // namespace

Translations::Translations(QObject* const parent)
    : QTranslator(parent)
{
    QFile catalog(kCatalog);
    if (!catalog.open(QIODevice::ReadOnly)) {
        throw std::runtime_error("the message catalog is missing from this build");
    }
    const QJsonObject document = QJsonDocument::fromJson(catalog.readAll()).object();

    for (const QJsonValue& entry : document.value(QStringLiteral("languages")).toArray()) {
        const QJsonObject language = entry.toObject();
        languages_.append(QVariantMap{
            {QStringLiteral("code"), language.value(QStringLiteral("code")).toString()},
            {QStringLiteral("name"), language.value(QStringLiteral("name")).toString()},
            {QStringLiteral("rtl"), language.value(QStringLiteral("rtl")).toBool()},
        });
    }

    const QJsonObject texts = document.value(QStringLiteral("messages")).toObject();
    for (auto it = texts.constBegin(); it != texts.constEnd(); ++it) {
        Entry entry;
        const QJsonObject perLanguage = it.value().toObject();
        for (auto text = perLanguage.constBegin(); text != perLanguage.constEnd(); ++text) {
            entry.byLanguage.insert(text.key(), text.value().toString());
        }
        messages_.insert(it.key(), entry);
    }

    const std::string chosen = AppSettings::instance().language();
    setLanguage(chosen.empty() ? systemLanguage() : QString::fromStdString(chosen));
}

QString Translations::systemLanguage() const
{
    for (const QString& wanted : preferredLanguages()) {
        const QString code = wanted.section(QChar(u'-'), 0, 0).toLower();
        for (const QVariant& known : languages_) {
            if (known.toMap().value(QStringLiteral("code")).toString() == code) {
                return code;
            }
        }
    }
    return kSourceLanguage;
}

void Translations::setLanguage(const QString& code)
{
    QString wanted = code;
    bool known = false;
    for (const QVariant& language : languages_) {
        known = known || language.toMap().value(QStringLiteral("code")).toString() == wanted;
    }
    if (!known) {
        wanted = kSourceLanguage;
    }
    if (language_ == wanted) {
        return;
    }
    language_ = wanted;
    emit languageChanged();
}

bool Translations::rightToLeft() const
{
    for (const QVariant& language : languages_) {
        const QVariantMap entry = language.toMap();
        if (entry.value(QStringLiteral("code")).toString() == language_) {
            return entry.value(QStringLiteral("rtl")).toBool();
        }
    }
    return false;
}

QString Translations::translate(const char* const context, const char* const sourceText,
    const char* const disambiguation, int) const
{
    Q_UNUSED(context);
    if (language_ == kSourceLanguage) {
        return {};
    }
    const auto found = messages_.constFind(keyFor(sourceText, disambiguation));
    if (found == messages_.constEnd()) {
        return {};
    }
    return found->byLanguage.value(language_);
}

}  // namespace bazarish::app
