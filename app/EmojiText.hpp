// Bazarish project (c) 2026
#pragma once

#include <QString>

namespace bazarish::app {

// Spaces between emoji allowed; 0 when anything else is in the text.
int emojiOnlyCount(const QString& text);

}  // namespace bazarish::app
