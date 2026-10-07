#!/bin/bash
# Bazarish project (c) 2026
# What Linux gets where no AppImage can be built: the binary with everything it
# needs beside it, in a folder. Debian 13 is the base because it is the oldest
# release whose own Qt builds this (6.8) and whose OpenSSL has Argon2id (3.5), and
# because Qt publishes no host tools for this architecture - a cross-built Qt
# cannot run moc here. The libc it asks for is therefore that of Debian 13.
set -eux
export DEBIAN_FRONTEND=noninteractive
export APPIMAGE_EXTRACT_AND_RUN=1
readonly kJobs=4
readonly kArch=$(uname -m)

apt-get update -qq
apt-get install -y -qq --no-install-recommends \
  build-essential cmake ninja-build git ca-certificates file rsync patchelf python3 pkg-config \
  qt6-base-dev qt6-declarative-dev qt6-multimedia-dev qt6-declarative-dev-tools \
  libssl-dev zlib1g-dev libsqlcipher-dev libopus-dev libqrencode-dev \
  libboost-dev libboost-program-options-dev libgl-dev libxkbcommon-dev \
  libxcb-cursor0 libxcb-icccm4 libxcb-image0 libxcb-keysyms1 libxcb-randr0 \
  libxcb-render-util0 libxcb-shape0 libxcb-xinerama0 libxcb-xkb1 libxkbcommon-x11-0 \
  libwayland-client0 libwayland-cursor0 libwayland-egl1 \
  libasound2t64 libpulse0 libsndfile1

# --delete, and not only a copy: a source file the repository no longer has must
# not stay behind in a directory that outlives one build.
rsync -a --delete --exclude 'build' --exclude 'build-*' --exclude 'AppDir' /host/ /src/
cd /src
git config --global --add safe.directory '*'

cmake -S . -B build-tarball -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-tarball -j"$kJobs" --target bazarish-app

APPDIR=/src/AppDir
rm -rf "$APPDIR"
mkdir -p "$APPDIR/usr/bin" "$APPDIR/usr/share/applications" \
         "$APPDIR/usr/share/icons/hicolor/512x512/apps"
cp build-tarball/bazarish-app "$APPDIR/usr/bin/"
cp app/icon/bazarish.png "$APPDIR/usr/share/icons/hicolor/512x512/apps/bazarish.png"
cat > "$APPDIR/usr/share/applications/bazarish.desktop" <<'DESKTOP'
[Desktop Entry]
Type=Application
Name=Bazarish
Comment=I2P messenger
Exec=bazarish-app %u
Icon=bazarish
Categories=Network;InstantMessaging;
MimeType=x-scheme-handler/bazarish;
Terminal=false
StartupWMClass=Bazarish
DESKTOP

# The same tool the AppImage uses, stopped one step earlier: it gathers Qt and
# the libraries the binary imports, and the folder is what is handed over.
mkdir -p /work
cd /work
cp "/tools/linuxdeploy-$kArch.AppImage" "/tools/linuxdeploy-plugin-qt-$kArch.AppImage" .
chmod +x "linuxdeploy-$kArch.AppImage" "linuxdeploy-plugin-qt-$kArch.AppImage"
export QML_SOURCES_PATHS=/src/app/qml
export EXTRA_QT_MODULES="multimedia"
"./linuxdeploy-$kArch.AppImage" --appdir "$APPDIR" \
  -e "$APPDIR/usr/bin/bazarish-app" \
  -d "$APPDIR/usr/share/applications/bazarish.desktop" \
  -i "$APPDIR/usr/share/icons/hicolor/512x512/apps/bazarish.png" \
  --exclude-library "libxcb-glx.so*" \
  --plugin qt

tar -czf "/out/Bazarish-linux-$kArch.tar.gz" -C "$APPDIR" .
chmod 0777 "/out/Bazarish-linux-$kArch.tar.gz"
ls -la /out
