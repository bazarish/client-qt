// Bazarish project (c) 2026
#pragma once

#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <QVector>

#include <optional>

namespace bazarish::app {

// QString positions, the '@' included.
struct MentionRange {
    int from = 0;
    int length = 0;
};

QVector<MentionRange> mentionRanges(const QString& text, const QStringList& names);

struct MentionTyping {
    int from = 0;
    QString typed;
};

// The mention being typed at the end of `before`, at least one character after its '@'.
std::optional<MentionTyping> mentionTypedAt(const QString& before);

QStringList mentionChoices(const QStringList& members, const QString& own, const QString& typed);

// {from, choices} for the mention typed at the end of `before`; empty when none is.
QVariantMap mentionOffer(const QString& before, const QStringList& members, const QString& own);

QString mentionText(const QString& name);

QStringList namesInOrder(QStringList names, const QString& language);

QStringList namesMatching(const QStringList& names, const QString& query);

}  // namespace bazarish::app
