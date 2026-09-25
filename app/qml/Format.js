// Bazarish project (c) 2026
.pragma library

// Values a person reads. One copy each: the byte size had grown four, and they
// had already drifted - two knew about terabytes and one stopped at megabytes.

const kBytesPerUnit = 1024
const kUnits = ["B", "KB", "MB", "GB", "TB"]
const kSecondsPerMinute = 60
// Below ten a second needs a leading zero to keep the colon in place.
const kPadBelow = 10

// "1.4 MB". Whole bytes below a kilobyte, one decimal above it.
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

// "4:07". Takes whole seconds: whether they were rounded or floored is the call
// site's decision, and it differs between counting up and counting down.
function minutesSeconds(totalSeconds) {
    const seconds = totalSeconds % kSecondsPerMinute
    return Math.floor(totalSeconds / kSecondsPerMinute) + ":"
        + (seconds < kPadBelow ? "0" : "") + seconds
}
