// Bazarish project (c) 2026
#pragma once

#include <QString>
#include <QStringList>

#include <vector>

namespace bazarish::app::markup {

enum class Action {
    eNone,
    eLink,
    eSend,
    eAlias,
    eCopy,
    eMention,
};

struct Run {
    QString text;
    QString target;
    Action action = Action::eNone;
    bool bold = false;
    bool italic = false;
    bool strike = false;
};

std::vector<Run> parse(const QString& text);

struct Colors {
    QString action;
    QString chip;
    QString code;
    QString codeText;
    QString mention;
};

std::vector<Run> parse(const QString& text, const QStringList& mentionNames);

QString toHtml(const QString& text, const Colors& colors, const QStringList& mentionNames = {});

QString toPlain(const QString& text);

}  // namespace bazarish::app::markup
