// Bazarish project (c) 2026
#include "PopupStack.hpp"

#include <QPointer>
#include <QQuickItem>
#include <QQuickWindow>
#include <QVariant>

#include <memory>
#include <stdexcept>
#include <vector>

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

void forgetFocusIn(QQuickItem* const scope)
{
    std::vector<QQuickItem*> chain;
    for (QQuickItem* at = scope->scopedFocusItem(); at != nullptr;
         at = at->isFocusScope() ? at->scopedFocusItem() : nullptr) {
        chain.push_back(at);
    }
    for (QQuickItem* const item : chain) {
        item->setFocus(false);
    }
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

void forgetFocusOnClose(QQuickItem* const overlay)
{
    const auto shown = std::make_shared<std::vector<QPointer<QQuickItem>>>();
    QObject::connect(overlay, &QQuickItem::childrenChanged, overlay, [overlay, shown]() {
        const QList<QQuickItem*> now = overlay->childItems();
        for (const QPointer<QQuickItem>& item : *shown) {
            if (item != nullptr && !now.contains(item.data()) && item->isFocusScope()) {
                forgetFocusIn(item);
            }
        }
        shown->assign(now.begin(), now.end());
    });
}

}  // namespace bazarish::app
