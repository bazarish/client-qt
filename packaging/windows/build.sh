#!/bin/bash
# Bazarish project (c) 2026
# Cross-builds the portable Windows client on Linux, x86_64 or arm64 hosts alike.
# Needs: python3 with aqtinstall, cmake, ninja, zip, zstd, git; nasm for an x86_64 target.
set -eu -o pipefail

readonly kUsage="usage: build.sh <x86_64|aarch64> <work dir> <output zip>"
readonly kArch=${1:?$kUsage}
readonly kWork=$(realpath -m "${2:?$kUsage}")
readonly kZip=$(realpath -m "${3:?$kUsage}")
readonly kSource=$(cd "$(dirname "$0")/../.." && pwd)
readonly kScripts=$kSource/packaging/windows
readonly kJobs=${BAZARISH_JOBS:-4}
readonly kLlvmMingwRelease=20261104
readonly kQtVersion=6.12.0
readonly kMsysRoot=https://repo.msys2.org/mingw
readonly kRoots=openssl,sqlcipher,opus,qrencode,boost,brotli,zlib,qt6-base,qt6-declarative,qt6-multimedia,qt6-svg,qt6-imageformats
readonly kFolder=Bazarish

case "$kArch" in
    x86_64) readonly kRepo=clang64 kPackagePrefix=mingw-w64-clang-x86_64- ;;
    aarch64) readonly kRepo=clangarm64 kPackagePrefix=mingw-w64-clang-aarch64- ;;
    *) echo "$kUsage" >&2; exit 2 ;;
esac
case "$(uname -m)" in
    x86_64) readonly kQtHost=linux kQtHostArch=linux_gcc_64 kQtHostDir=gcc_64 ;;
    aarch64) readonly kQtHost=linux_arm64 kQtHostArch=linux_gcc_arm64 kQtHostDir=gcc_arm64 ;;
    *) echo "build.sh: no host Qt for $(uname -m)" >&2; exit 2 ;;
esac
if [ "$kArch" = x86_64 ] && ! command -v nasm > /dev/null; then
    echo "build.sh: BoringSSL's x86_64 assembly for Windows needs nasm" >&2
    exit 2
fi

readonly kLlvmMingw=$kWork/llvm-mingw-$kLlvmMingwRelease-ucrt-ubuntu-22.04-$(uname -m)
readonly kSysroot=$kWork/sysroot-$kRepo
readonly kQtHostPath=$kWork/hostqt/$kQtVersion/$kQtHostDir
readonly kBuild=$kWork/build-$kArch
mkdir -p "$kWork"

if [ ! -x "$kLlvmMingw/bin/clang" ]; then
    curl -fsSL "https://github.com/mstorsjo/llvm-mingw/releases/download/$kLlvmMingwRelease/$(basename "$kLlvmMingw").tar.xz" \
        | tar -xJ -C "$kWork"
fi

if [ ! -d "$kSysroot" ]; then
    rm -rf "$kWork/db-$kRepo"
    mkdir -p "$kWork/db-$kRepo"
    curl -fsSL "$kMsysRoot/$kRepo/$kRepo.db" | tar --zstd -x -C "$kWork/db-$kRepo"
    mkdir -p "$kSysroot.partial"
    python3 -I "$kScripts/packages.py" "$kMsysRoot/$kRepo" "$kWork/db-$kRepo" "$kPackagePrefix" \
        "$kWork/packages-$kRepo" "$kRoots" \
        | while read -r package; do tar --zstd -x -C "$kSysroot.partial" -f "$package"; done
    mv "$kSysroot.partial/$kRepo" "$kSysroot"
    rm -rf "$kSysroot.partial" "$kWork/db-$kRepo"
fi

if [ ! -x "$kQtHostPath/libexec/qmlimportscanner" ]; then
    python3 -m aqt install-qt "$kQtHost" desktop "$kQtVersion" "$kQtHostArch" \
        -m qtshadertools qtmultimedia -O "$kWork/hostqt"
fi

export BAZARISH_WIN_ARCH=$kArch BAZARISH_LLVM_MINGW=$kLlvmMingw BAZARISH_WIN_SYSROOT=$kSysroot \
    BAZARISH_QT_HOST=$kQtHostPath
cmake -S "$kSource" -B "$kBuild" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_TOOLCHAIN_FILE="$kScripts/toolchain.cmake" \
    -DSQLCIPHER_LIBRARY="$kSysroot/lib/libsqlcipher.dll.a" -DSQLCIPHER_INCLUDE_DIR="$kSysroot/include"
cmake --build "$kBuild" -j "$kJobs" --target bazarish

"$kQtHostPath/libexec/qmlimportscanner" -rootPath "$kSource/app/qml" \
    -importPath "$kSysroot/share/qt6/qml" > "$kWork/qml-imports.json"
rm -rf "$kWork/out"
python3 -I "$kScripts/deploy.py" "$kWork/out/$kFolder" "$kBuild/bazarish.exe" \
    "$kWork/qml-imports.json" "$kLlvmMingw" "$kSysroot" "$kArch-w64-mingw32"
rm -f "$kZip"
(cd "$kWork/out" && zip -q -r -9 "$kZip" "$kFolder")
rm -rf "$kWork/out" "$kWork/qml-imports.json"
echo "== $kZip"
