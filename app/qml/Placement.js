// Bazarish project (c) 2026
.pragma library

function sceneRect(item) {
    return item.mapToItem(null, 0, 0, item.width, item.height)
}

function sideX(bubble, size, area, gap, leftFirst) {
    const leftX = bubble.x - gap - size.width
    const rightX = bubble.x + bubble.width + gap
    const fitsLeft = leftX >= area.x
    const fitsRight = rightX + size.width <= area.x + area.width
    const sides = leftFirst ? [[fitsLeft, leftX], [fitsRight, rightX]]
        : [[fitsRight, rightX], [fitsLeft, leftX]]
    const fitting = sides.find(side => side[0])
    return { fits: fitting !== undefined, x: fitting ? fitting[1] : sides[0][1] }
}

function clampX(x, size, area) {
    return Math.max(area.x, Math.min(x, area.x + area.width - size.width))
}

function clampY(y, size, area) {
    return Math.max(area.y, Math.min(y, area.y + area.height - size.height))
}

function besideBubble(bubble, size, area, gap, leftFirst) {
    const side = sideX(bubble, size, area, gap, leftFirst)
    return Qt.point(clampX(side.x, size, area), clampY(bubble.y, size, area))
}

// The bubble's time and device sit on its right.
function besideOrOverBubble(bubble, size, area, gap, leftFirst) {
    const side = sideX(bubble, size, area, gap, leftFirst)
    const x = side.fits ? side.x : area.x
    return Qt.point(clampX(x, size, area), clampY(bubble.y, size, area))
}
