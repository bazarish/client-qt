// Bazarish project (c) 2026
#pragma once

#include <QString>

namespace bazarish::app {

// How many emoji the text is made of, spaces between them allowed; 0 when anything else is in it.
int emojiOnlyCount(const QString& text);

}  // namespace bazarish::app
