// Bazarish project (c) 2026
#pragma once

class QObject;

namespace bazarish::app {

// Sends a text field's selection again as the input method's: a field then keeps it when the
// finger that made it lifts, where a touch release would otherwise put the cursor there.
void holdSelection(QObject* field);

}  // namespace bazarish::app
