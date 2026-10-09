#!/bin/bash
# Bazarish project (c) 2026
# One recipe for every platform the client ships to, in the three stages a
# workflow reports separately: what each one cost is then visible without the log.
set -eu -o pipefail

readonly kJobs=4
readonly kTestSeconds=300

stage=${1:?usage: build.sh configure|build|test}
# One log per stage: the reporter reads the newest, so it never explains a
# failure with the step before it.
readonly kLog=ci-$stage.log

configure=(-S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release)
if [ "$(uname)" = "Darwin" ]; then
    # Homebrew keeps these out of the default prefix, so each one is named.
    configure+=(
        "-DCMAKE_PREFIX_PATH=$(brew --prefix qt);$(brew --prefix opus);$(brew --prefix qrencode);$(brew --prefix boost)"
        "-DOPENSSL_ROOT_DIR=$(brew --prefix openssl@3)"
        "-DSQLCIPHER_LIBRARY=$(brew --prefix sqlcipher)/lib/libsqlcipher.dylib"
        "-DSQLCIPHER_INCLUDE_DIR=$(brew --prefix sqlcipher)/include"
    )
fi

{
    case "$stage" in
    configure) cmake "${configure[@]}" ;;
    build) cmake --build build -j "$kJobs" ;;
    test) ctest --test-dir build -j "$kJobs" --output-on-failure --timeout "$kTestSeconds" ;;
    *) echo "no such stage: $stage" >&2; exit 2 ;;
    esac
} 2>&1 | tee -a "$kLog"
