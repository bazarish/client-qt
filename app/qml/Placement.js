// Bazarish project (c) 2026
.pragma library

function sceneRect(item) {
    return item.mapToItem(null, 0, 0, item.width, item.height)
}

function besideBubble(bubble, size, area, gap, leftFirst) {
    const leftX = bubble.x - gap - size.width
    const rightX = bubble.x + bubble.width + gap
    const fitsLeft = leftX >= area.x
    const fitsRight = rightX + size.width <= area.x + area.width
    const sides = leftFirst ? [[fitsLeft, leftX], [fitsRight, rightX]]
        : [[fitsRight, rightX], [fitsLeft, leftX]]
    const fitting = sides.find(side => side[0])
    const wanted = fitting ? fitting[1] : sides[0][1]
    const x = Math.max(area.x, Math.min(wanted, area.x + area.width - size.width))
    const y = Math.max(area.y, Math.min(bubble.y, area.y + area.height - size.height))
    return Qt.point(x, y)
}
