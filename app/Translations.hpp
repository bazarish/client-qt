// Bazarish project (c) 2026
#pragma once

#include <QHash>
#include <QObject>
#include <QString>
#include <QTranslator>
#include <QVariantList>

class QQmlApplicationEngine;

namespace bazarish::app {

class Translations : public QTranslator {
    Q_OBJECT
    Q_PROPERTY(QString language READ language WRITE setLanguage NOTIFY languageChanged)
    Q_PROPERTY(QVariantList languages READ languages CONSTANT)
    Q_PROPERTY(bool rightToLeft READ rightToLeft NOTIFY languageChanged)
public:
    explicit Translations(QQmlApplicationEngine& engine, QObject* parent = nullptr);

    QString translate(const char* context, const char* sourceText, const char* disambiguation,
        int n) const override;
    bool isEmpty() const override { return false; }

    QString language() const { return language_; }
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

    QQmlApplicationEngine& engine_;
    QVariantList languages_;
    QHash<QString, Entry> messages_;
    QString language_;
};

}  // namespace bazarish::app
