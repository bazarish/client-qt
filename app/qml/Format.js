// Bazarish project (c) 2026
.pragma library

const kBytesPerUnit = 1024
const kUnits = ["B", "KB", "MB", "GB", "TB"]
const kSecondsPerMinute = 60
const kPadBelow = 10

function bytes(n) {
    if (!n || n <= 0) {
        return "0 B"
    }
    var value = n
    var unit = 0
    while (value >= kBytesPerUnit && unit < kUnits.length - 1) {
        value /= kBytesPerUnit
        unit++
    }
    return (unit === 0 ? value : value.toFixed(1)) + " " + kUnits[unit]
}

function minutesSeconds(totalSeconds) {
    const seconds = totalSeconds % kSecondsPerMinute
    return Math.floor(totalSeconds / kSecondsPerMinute) + ":"
        + (seconds < kPadBelow ? "0" : "") + seconds
}
