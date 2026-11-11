// Bazarish project (c) 2026
.pragma library

const kSecondsPerMinute = 60
const kSecondsPerHour = 3600
const kMillisPerSecond = 1000
const kPadBelow = 10

function minutesSeconds(totalSeconds) {
    const seconds = totalSeconds % kSecondsPerMinute
    return Math.floor(totalSeconds / kSecondsPerMinute) + ":"
        + (seconds < kPadBelow ? "0" : "") + seconds
}

function callClock(connectedAtMs, nowMs) {
    const total = Math.max(0, Math.floor((nowMs - connectedAtMs) / kMillisPerSecond))
    const seconds = total % kSecondsPerMinute
    const minutes = Math.floor(total / kSecondsPerMinute) % kSecondsPerMinute
    const hours = Math.floor(total / kSecondsPerHour)
    const mm = (hours > 0 && minutes < kPadBelow ? "0" : "") + minutes
    const ss = (seconds < kPadBelow ? "0" : "") + seconds
    return (hours > 0 ? hours + ":" : "") + mm + ":" + ss
}
