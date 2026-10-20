// Bazarish project (c) 2026
#include "SelectionHold.hpp"

#include <QCoreApplication>
#include <QInputMethodEvent>
#include <QInputMethodQueryEvent>

#include <stdexcept>

namespace bazarish::app {

void holdSelection(QObject* const field)
{
    QInputMethodQueryEvent query(Qt::ImCursorPosition | Qt::ImAnchorPosition);
    QCoreApplication::sendEvent(field, &query);
    const QVariant cursorValue = query.value(Qt::ImCursorPosition);
    const QVariant anchorValue = query.value(Qt::ImAnchorPosition);
    if (!cursorValue.isValid() || !anchorValue.isValid()) {
        throw std::invalid_argument("a selection is held only in a text field");
    }
    // Both are within the current block, which is where the event's selection is read.
    const int cursor = cursorValue.toInt();
    const int anchor = anchorValue.toInt();
    QInputMethodEvent event(QString(), {{QInputMethodEvent::Selection, anchor, cursor - anchor, QVariant()}});
    QCoreApplication::sendEvent(field, &event);
}

}  // namespace bazarish::app
