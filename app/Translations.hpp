// Bazarish project (c) 2026
#pragma once

#include <QHash>
#include <QObject>
#include <QReadWriteLock>
#include <QString>
#include <QTranslator>
#include <QVariantList>

namespace bazarish::app {

class Translations : public QTranslator {
    Q_OBJECT
    Q_PROPERTY(QString language READ language WRITE setLanguage NOTIFY languageChanged)
    Q_PROPERTY(QVariantList languages READ languages CONSTANT)
    Q_PROPERTY(bool rightToLeft READ rightToLeft NOTIFY languageChanged)
public:
    explicit Translations(QObject* parent = nullptr);

    QString translate(const char* context, const char* sourceText, const char* disambiguation,
        int n) const override;
    bool isEmpty() const override { return false; }

    QString language() const;
    void setLanguage(const QString& code);

    QVariantList languages() const { return languages_; }
    bool rightToLeft() const;

    QString systemLanguage() const;

signals:
    void languageChanged();

private:
    struct Entry {
        QHash<QString, QString> byLanguage;
    };

    QVariantList languages_;
    QHash<QString, Entry> messages_;
    // tr() reads it on any thread, the settings page writes it on the GUI thread.
    mutable QReadWriteLock languageLock_;
    QString language_;
};

}  // namespace bazarish::app
