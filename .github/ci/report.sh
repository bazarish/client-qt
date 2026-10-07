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

# A failed test run names the tests after saying that some failed, so that part
# of the log is taken whole; otherwise the compiler's own lines are what matter.
said=$(sed -n '/The following tests FAILED/,$p' "$kLog")
if [ -z "$said" ]; then
    said=$(grep -aE 'error:|error [A-Z][0-9]|FAILED:|CMake Error' "$kLog" | tail -n "$kLines")
fi
if [ -z "$said" ]; then
    said=$(tail -n "$kLines" "$kLog")
fi
printf '%s\n' "$said" | grep -v '^[[:space:]]*$' | sed 's/^/::error::/'
