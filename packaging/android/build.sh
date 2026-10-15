#!/bin/bash
# Bazarish project (c) 2026
# The Android package from a checkout: the libraries, the client and a signed APK.
# The toolchain is named by the environment, so CI and a workstation run this alike.
set -eu -o pipefail

readonly kJobs=${BAZARISH_JOBS:-4}
readonly kSdk=${ANDROID_SDK_ROOT:?the Android SDK, as ANDROID_SDK_ROOT}
readonly kNdk=${ANDROID_NDK_ROOT:?the NDK, as ANDROID_NDK_ROOT}
readonly kQt=${QT_ANDROID_ROOT:?Qt for Android arm64-v8a, as QT_ANDROID_ROOT}
readonly kQtHost=${QT_HOST_ROOT:?the desktop Qt of the same version, as QT_HOST_ROOT}
readonly kBuildTools=${ANDROID_BUILD_TOOLS:?the build-tools version, as ANDROID_BUILD_TOOLS}
readonly kKeystore=${BAZARISH_ANDROID_KEYSTORE:?the keystore to sign with}
readonly kKeyAlias=${BAZARISH_ANDROID_KEY_ALIAS:?its key alias}
readonly kKeyPass=${BAZARISH_ANDROID_KEY_PASS:?its password}
readonly kOut=${1:?usage: build.sh <output dir>}
readonly kSource=$(cd "$(dirname "$0")/../.." && pwd)
readonly kPrefix=$kOut/prefix
readonly kBuild=$kOut/build
readonly kApk=$kOut/Bazarish-android-arm64.apk

"$kSource/packaging/android/deps.sh" "$kPrefix" "$kOut/work"

"$kQt/bin/qt-cmake" -S "$kSource" -B "$kBuild" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DQT_HOST_PATH="$kQtHost" -DANDROID_SDK_ROOT="$kSdk" -DANDROID_NDK_ROOT="$kNdk" \
    -DCMAKE_FIND_ROOT_PATH="$kPrefix" -DCMAKE_PREFIX_PATH="$kPrefix" \
    -DOPENSSL_ROOT_DIR="$kPrefix" -DOPENSSL_USE_STATIC_LIBS=TRUE \
    -DSQLCIPHER_LIBRARY="$kPrefix/lib/libsqlcipher.a" -DSQLCIPHER_INCLUDE_DIR="$kPrefix/include"
cmake --build "$kBuild" --target apk -j "$kJobs"

readonly kTools=$kSdk/build-tools/$kBuildTools
"$kTools/zipalign" -p -f 4 \
    "$kBuild/android-build/build/outputs/apk/release/android-build-release-unsigned.apk" \
    "$kOut/aligned.apk"
"$kTools/apksigner" sign --ks "$kKeystore" --ks-key-alias "$kKeyAlias" \
    --ks-pass "pass:$kKeyPass" --out "$kApk" "$kOut/aligned.apk"
rm -f "$kOut/aligned.apk"
"$kTools/apksigner" verify "$kApk"
echo "== $kApk"
