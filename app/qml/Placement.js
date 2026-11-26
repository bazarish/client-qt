// Bazarish project (c) 2026
.pragma library

function sceneRect(item) {
    return item.mapToItem(null, 0, 0, item.width, item.height)
}

function clampX(x, size, area) {
    return Math.max(area.x, Math.min(x, area.x + area.width - size.width))
}

function clampY(y, size, area) {
    return Math.max(area.y, Math.min(y, area.y + area.height - size.height))
}

// Across the middle of the chat, under the bubble or else over it, clear of the chat's edges.
function underOrOverBubble(bubble, size, area, gap, margin) {
    const inner = Qt.rect(area.x + margin, area.y + margin,
        area.width - 2 * margin, area.height - 2 * margin)
    const under = bubble.y + bubble.height + gap
    const over = bubble.y - gap - size.height
    const y = under + size.height <= inner.y + inner.height || over < inner.y ? under : over
    const x = inner.x + (inner.width - size.width) / 2
    return Qt.point(clampX(x, size, inner), clampY(y, size, inner))
}

// Always on the one side; short of room it covers the bubble instead of crossing over.
function besideBubble(bubble, size, area, gap, onLeft) {
    const x = onLeft ? bubble.x - gap - size.width : bubble.x + bubble.width + gap
    return Qt.point(clampX(x, size, area), clampY(bubble.y, size, area))
}
