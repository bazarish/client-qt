#!/bin/bash
# Bazarish project (c) 2026
# Where a test that passed sits when its process does not end: every thread's stack.
set -u

readonly kWaitSeconds=60
readonly kLines=200

test=${1:?usage: stacks.sh <test executable>}
"$test" > ci-stacks.log 2>&1 &
pid=$!
for _ in $(seq "$kWaitSeconds"); do
    if ! kill -0 "$pid" 2>/dev/null; then
        echo "::notice::$test ended by itself"
        exit 0
    fi
    sleep 1
done
gdb -batch -p "$(cat "/proc/$pid/winpid")" -ex "info threads" -ex "thread apply all bt" \
    >> ci-stacks.log 2>&1
kill "$pid"
body=$(tail -n "$kLines" ci-stacks.log)
body=${body//%/%25}
body=${body//$'\r'/%0D}
body=${body//$'\n'/%0A}
printf '::error::%s did not end:%%0A%s\n' "$test" "$body"
