#!/bin/bash
# Bazarish project (c) 2026
# One bundle for both architectures, made from the two that were built natively:
# every Mach-O file inside it carries the code of both.
set -eu

readonly kArm=${1:?usage: macos-universal.sh <arm64.app> <x86_64.app> <out.app>}
readonly kIntel=${2:?}
readonly kOut=${3:?}

test -d "$kArm" && test -d "$kIntel"
rm -rf "$kOut"
cp -R "$kArm" "$kOut"

while IFS= read -r path; do
    file "$kOut/$path" | grep -q Mach-O || continue
    if [ ! -f "$kIntel/$path" ]; then
        echo "$path is in one bundle and not in the other" >&2
        exit 1
    fi
    lipo -create "$kOut/$path" "$kIntel/$path" -output "$kOut/$path.both"
    mv "$kOut/$path.both" "$kOut/$path"
done < <(cd "$kOut" && find . -type f -print | sed 's|^\./||')

lipo -archs "$kOut/Contents/MacOS/bazarish-app"
