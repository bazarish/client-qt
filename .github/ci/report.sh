#!/bin/bash
# Bazarish project (c) 2026
# What the failed step said, as annotations: a run's log is readable only with
# rights on the repository, while an annotation is readable by anyone who can
# read the repository itself.
set -u

readonly kLines=25
readonly kLog=ci.log

if [ ! -f "$kLog" ]; then
    echo "::error::the step left no log behind"
    exit 0
fi

# In the order that tells the most: a failed check or a compiler error names the
# place, and a test run names the tests it failed.
said=$(grep -aE 'CHECK failed|error:|error [A-Z][0-9]|FAILED:|CMake Error' "$kLog" \
    | tail -n "$kLines")
failures=$(sed -n '/The following tests FAILED/,$p' "$kLog")
if [ -n "$failures" ]; then
    said=$(printf '%s\n%s' "$said" "$failures")
fi
if [ -z "$said" ]; then
    said=$(tail -n "$kLines" "$kLog")
fi
printf '%s\n' "$said" | grep -v '^[[:space:]]*$' | sed 's/^/::error::/'
