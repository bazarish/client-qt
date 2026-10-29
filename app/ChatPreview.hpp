// Bazarish project (c) 2026
#pragma once

#include "TranscriptStore.hpp"

#include <QString>

namespace bazarish::app {

// The last message as one line: its text, or what it carries, after the author in a group.
QString chatPreviewOf(const TranscriptStore::LastMessage& last);

}  // namespace bazarish::app
