#!/bin/bash
# Bazarish project (c) 2026
# What the failed step said, as annotations: a run's log is readable only with
# rights on the repository, while an annotation is readable by anyone who can
# read the repository itself.
set -u

# GitHub keeps ten error annotations of a step and drops the rest, so what is
# printed has to be the ten lines that name the failure.
readonly kLines=9

# Each step writes its own log, so the newest one is the step that just failed.
log=$(ls -t ci-*.log 2>/dev/null | head -n 1)
if [ -z "$log" ]; then
    echo "::error::the step left no log behind"
    exit 0
fi
if [ ! -s "$log" ]; then
    echo "::error::$log is empty: the step printed nothing before it failed"
    exit 0
fi

# The lines around the first mark, which is where the failure is named: for a
# compile error the lines above it are the include chain that says which file
# asked for it, and for a test the lines above it are what the test itself
# printed before it stopped.
window() {
    local pattern=$1 before=$2 after=$3
    local at
    at=$(grep -naE "$pattern" "$log" | head -n 1 | cut -d: -f1)
    if [ -n "$at" ]; then
        sed -n "$((at > before ? at - before : 1)),$((at + after))p" "$log"
    fi
}

# Ninja's own "FAILED:" line is not a mark: it comes first and would push the
# error that follows it out of frame.
failures=$(sed -n '/The following tests FAILED/,$p' "$log")
if [ -n "$failures" ]; then
    said=$(printf '%s\n%s' "$(window '\*\*\*(Timeout|Failed|Exception)' 4 1)" "$failures")
else
    said=$(window 'CHECK failed|error:|error [A-Z]+[0-9]+|CMake Error' 6 3)
fi
if [ -z "${said//[[:space:]]/}" ]; then
    said=$(tail -n "$kLines" "$log")
fi

printf '::error::%s said:\n' "$log"
printf '%s\n' "$said" | grep -v '^[[:space:]]*$' | head -n "$kLines" | sed 's/^/::error::/'
