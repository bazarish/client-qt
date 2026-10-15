#!/bin/bash
# Bazarish project (c) 2026
# Why a router test that passed is not seen to end under ctest on Windows: the
# same test run three ways, and the stacks of whatever is still there.
set -u

readonly kWaitSeconds=60
readonly kLines=250

bin=build/bazarish-common
: > ci-stacks.log
note() { printf '%s\n' "$*" >> ci-stacks.log; }

ended() {
    local pid=$1
    for _ in $(seq "$kWaitSeconds"); do
        kill -0 "$pid" 2>/dev/null || return 0
        sleep 1
    done
    return 1
}

stacks() {
    local winpid
    for winpid in $(ps -W | awk '/TestI2p(Proxy|Reseed)/ {print $4}'); do
        note "== stacks of $winpid"
        gdb -batch -p "$winpid" -ex "info threads" -ex "thread apply all bt" >> ci-stacks.log 2>&1
    done
}

"$bin/TestI2pProxy.exe" > proxy-alone.log 2>&1 &
a=$!
"$bin/TestI2pReseed.exe" > reseed-alone.log 2>&1 &
b=$!
ended "$a" && note "both at once: the proxy test ended" || { note "both at once: the proxy test hangs"; stacks; }
ended "$b" && note "both at once: the reseed test ended" || note "both at once: the reseed test hangs"
kill "$a" "$b" 2>/dev/null

"$bin/TestI2pProxy.exe" 2>&1 | cat > proxy-pipe.log &
p=$!
ended "$p" && note "through a pipe: ended" || { note "through a pipe: hangs"; stacks; }
kill "$p" 2>/dev/null

ctest --test-dir build -R '^TestI2pProxy$' --timeout 120 > ctest-alone.log 2>&1 &
c=$!
if ended "$c"; then
    note "under ctest alone: ended"
else
    note "under ctest alone: hangs; processes:"
    ps -W | grep -iE 'TestI2p|ctest' >> ci-stacks.log
    stacks
fi
kill "$c" 2>/dev/null
for log in ctest-alone.log; do note "== $log"; tail -n 20 "$log" >> ci-stacks.log; done

body=$(tail -n "$kLines" ci-stacks.log)
body=${body//%/%25}
body=${body//$'\r'/%0D}
body=${body//$'\n'/%0A}
printf '::error::what the router tests do on their own:%%0A%s\n' "$body"
