// Bazarish project (c) 2026
#pragma once

#include <QString>

#include <vector>

namespace bazarish::app::markup {

// What a click on a run does; a plain text run does nothing.
enum class Action {
    eNone,
    // An http(s) address, opened outside the application after a warning.
    eLink,
    // Text the message offers to send back as it stands.
    eSend,
    // A username, offered to the add-a-contact form. Nothing is sent.
    eAlias,
};

// One stretch of a message body under one set of styles.
struct Run {
    QString text;
    // What a click acts on: the address, the text to send, or the username.
    QString target;
    Action action = Action::eNone;
    bool bold = false;
    bool italic = false;
    bool strike = false;
};

// Splits a message body into runs. What is not well-formed markup is text, so
// there is nothing here that can fail.
std::vector<Run> parse(const QString& text);

// The body as rich text for a bubble. Every character of the message is escaped
// and the tags are ours alone: the text comes from a correspondent, and a
// document that accepted their tags would fetch what they named in one.
QString toHtml(const QString& text, const QString& actionColor, const QString& chipColor);

// The body with the markers taken out: what a preview, a reply quote or a search
// hit shows - one line of what was said rather than a rendering of it.
QString toPlain(const QString& text);

}  // namespace bazarish::app::markup
