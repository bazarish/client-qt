// Bazarish project (c) 2026
#pragma once

#include "TranscriptStore.hpp"

#include <QString>

namespace bazarish::app {

QString chatPreviewOf(const TranscriptStore::LastMessage& last);

}  // namespace bazarish::app
