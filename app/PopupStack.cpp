// Bazarish project (c) 2026
#include "PopupStack.hpp"

#include <QQuickItem>
#include <QQuickWindow>
#include <QVariant>

#include <stdexcept>

namespace bazarish::app {
namespace {

// Popup.CloseOnEscape in Qt Quick Controls' ClosePolicy.
constexpr int kCloseOnEscape = 0x10;

QQuickItem* overlayOf(QQuickWindow* const window)
{
    for (QQuickItem* const child : window->contentItem()->childItems()) {
        if (child->inherits("QQuickOverlay")) {
            return child;
        }
    }
    return nullptr;
}

}  // namespace

bool closeTopPopup(QQuickWindow* const window)
{
    // Qt Quick Controls keeps its popup list private; an open popup's item is a child of the overlay.
    QQuickItem* const overlay = overlayOf(window);
    if (overlay == nullptr) {
        return false;
    }
    const QList<QQuickItem*> stacked = overlay->childItems();
    QObject* top = nullptr;
    qreal topZ = 0;
    qsizetype topIndex = -1;
    for (QObject* const popup : window->findChildren<QObject*>()) {
        if (!popup->inherits("QQuickPopup") || !popup->property("opened").toBool()) {
            continue;
        }
        QQuickItem* item = qvariant_cast<QQuickItem*>(popup->property("contentItem"));
        while (item != nullptr && item->parentItem() != overlay) {
            item = item->parentItem();
        }
        const qsizetype index = stacked.indexOf(item);
        const qreal z = popup->property("z").toReal();
        if (top == nullptr || z > topZ || (z == topZ && index > topIndex)) {
            top = popup;
            topZ = z;
            topIndex = index;
        }
    }
    if (top == nullptr) {
        return false;
    }
    if ((top->property("closePolicy").toInt() & kCloseOnEscape) != 0
        && !QMetaObject::invokeMethod(top, "close")) {
        throw std::runtime_error("a popup did not take close()");
    }
    return true;
}

}  // namespace bazarish::app
