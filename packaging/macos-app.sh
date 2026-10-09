#!/bin/bash
# Bazarish project (c) 2026
# The application bundle for one architecture, with Qt and the libraries it
# imports carried inside it.
set -eu

readonly kBuild=${1:-build}
readonly kApp="$kBuild/bazarish-app.app"

if [ ! -d "$kApp" ]; then
    echo "there is no bundle at $kApp" >&2
    exit 1
fi
"$(brew --prefix qt)/bin/macdeployqt" "$kApp" -qmldir=app/qml
