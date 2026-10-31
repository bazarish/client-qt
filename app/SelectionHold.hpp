// Bazarish project (c) 2026
#pragma once

class QObject;

namespace bazarish::app {

// A touch release would otherwise put the cursor where the selecting finger lifts.
void holdSelection(QObject* field);

}  // namespace bazarish::app
