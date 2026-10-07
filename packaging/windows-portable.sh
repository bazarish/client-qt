#!/bin/bash
# Bazarish project (c) 2026
# The portable Windows folder: the binary, what Qt needs beside it, and every
# library the two of them import. Nothing is resolved through MSYS2 at run time,
# so the folder runs on a machine that has never seen it.
set -eu

readonly kOut=${1:?usage: windows-portable.sh <folder> [build dir]}
readonly kBuild=${2:-build}
readonly kBinary=bazarish-app.exe
readonly kPrefix="${MSYSTEM_PREFIX:?run this from an MSYS2 shell}/bin"

objdump=$(command -v objdump || command -v llvm-objdump)

test -x "$kBuild/$kBinary"
rm -rf "$kOut"
mkdir -p "$kOut"
cp "$kBuild/$kBinary" "$kOut/"

windeployqt6 --qmldir app/qml --compiler-runtime "$kOut/$kBinary"

# windeployqt brings Qt and its plugins; the rest of the closure - OpenSSL,
# SQLCipher, Opus, qrencode, Boost, the toolchain runtime - is walked here. A
# library copied in brings imports of its own, so this runs until a round adds
# nothing.
while :; do
    added=0
    while read -r name; do
        test -e "$kOut/$name" && continue
        test -e "$kPrefix/$name" || continue
        cp "$kPrefix/$name" "$kOut/"
        added=1
    done < <(find "$kOut" \( -name '*.exe' -o -name '*.dll' \) -print0 \
        | xargs -0 "$objdump" -p \
        | sed -n 's/^\s*DLL Name:\s*//p' | sort -u)
    test "$added" = 0 && break
done

# Qt looks beside the binary rather than at the prefix it was built with.
printf '[Paths]\nPrefix = .\nPlugins = .\nImports = .\nQml2Imports = .\n' > "$kOut/qt.conf"

du -sh "$kOut"
