// Bazarish project (c) 2026
.pragma library

const kSecondsPerMinute = 60
const kPadBelow = 10

function minutesSeconds(totalSeconds) {
    const seconds = totalSeconds % kSecondsPerMinute
    return Math.floor(totalSeconds / kSecondsPerMinute) + ":"
        + (seconds < kPadBelow ? "0" : "") + seconds
}
