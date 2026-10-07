#!/bin/bash
# Bazarish project (c) 2026
# Configure, build and test, with everything the step printed kept for the
# reporter beside it. One recipe for every platform the client ships to.
set -eu -o pipefail

readonly kJobs=4
readonly kLog=ci.log

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
    cmake "${configure[@]}"
    cmake --build build -j "$kJobs"
    ctest --test-dir build -j "$kJobs" --output-on-failure
} 2>&1 | tee "$kLog"
