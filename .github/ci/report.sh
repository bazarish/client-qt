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

said=$(grep -aE 'error:|error [A-Z][0-9]|FAILED:|CMake Error|The following tests FAILED' \
    "$kLog" | tail -n "$kLines")
if [ -z "$said" ]; then
    said=$(tail -n "$kLines" "$kLog")
fi
printf '%s\n' "$said" | grep -v '^[[:space:]]*$' | sed 's/^/::error::/'
