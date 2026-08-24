#!/bin/bash
# Portable Bazarish AppImage. Built on Ubuntu 24.04 (glibc 2.39, GCC 13 for
# std::format) against an upstream Qt 6.8 (the code calls loadFromModule, which
# is Qt 6.5+, while 24.04 ships 6.4): the oldest glibc that can build it, with a
# Qt new enough for it, bundled so the host needs neither.
set -eux
export DEBIAN_FRONTEND=noninteractive
export APPIMAGE_EXTRACT_AND_RUN=1

apt-get update -qq
# qt6-*-dev is installed for the system libraries Qt itself needs (X, GL,
# fontconfig, audio); the build itself uses the Qt under /opt.
apt-get install -y -qq --no-install-recommends \
  build-essential cmake git ca-certificates wget file rsync patchelf python3-pip \
  libssl-dev zlib1g-dev libsqlcipher-dev libopus-dev libqrencode-dev \
  libboost-dev libboost-program-options-dev \
  qt6-base-dev qt6-declarative-dev qt6-multimedia-dev libgl-dev libxkbcommon-dev \
  libxcb-cursor0 libxcb-icccm4 libxcb-image0 libxcb-keysyms1 libxcb-randr0 \
  libxcb-render-util0 libxcb-shape0 libxcb-xinerama0 libxcb-xkb1 libxkbcommon-x11-0 \
  libwayland-client0 libwayland-cursor0 libwayland-egl1 \
  libxrandr2 libxext6 libxfixes3 libxdamage1 libxi6 libxtst6 libxrender1 libsm6 libice6 \
  libva2 libva-drm2 libva-x11-2 libvdpau1 libdrm2 libasound2t64 libpulse0 libsndfile1

QTDIR=/opt/Qt/6.8.2/gcc_64
if [ ! -x "$QTDIR/bin/qmake" ]; then
    pip install --break-system-packages -q aqtinstall
    aqt install-qt linux desktop 6.8.2 linux_gcc_64 \
        -m qtmultimedia qtshadertools -O /opt/Qt
fi
test -x "$QTDIR/bin/qmake"

# OpenSSL: 24.04 ships 3.0, which has no Argon2id (3.2+), and a account key is
# Argon2id. Built and bundled here so the host's version does not decide whether
# a account can be created at all.
OPENSSL_VERSION=3.5.7
if [ ! -f /opt/openssl/lib64/libcrypto.so.3 ]; then
    cd /work-openssl 2>/dev/null || { mkdir -p /work-openssl; cd /work-openssl; }
    wget -q "https://github.com/openssl/openssl/releases/download/openssl-${OPENSSL_VERSION}/openssl-${OPENSSL_VERSION}.tar.gz"
    tar xf "openssl-${OPENSSL_VERSION}.tar.gz"
    cd "openssl-${OPENSSL_VERSION}"
    ./Configure --prefix=/opt/openssl --openssldir=/opt/openssl/ssl shared no-docs
    make -j"$(nproc)"
    make install_sw
fi
export LD_LIBRARY_PATH=/opt/openssl/lib64:${LD_LIBRARY_PATH:-}

rsync -a --exclude 'build' --exclude 'build-asan' --exclude 'AppDir' /host/ /src/
cd /src
git config --global --add safe.directory '*'

# -U clears any OpenSSL paths a previous configure cached: the root below is
# only consulted when they are not already set.
cmake -S . -B build-appimage -DCMAKE_BUILD_TYPE=Release -UOPENSSL_* \
    -DCMAKE_PREFIX_PATH="$QTDIR" -DOPENSSL_ROOT_DIR=/opt/openssl
cmake --build build-appimage -j"$(nproc)" --target bazarish-app

APPDIR=/src/AppDir
rm -rf "$APPDIR"
mkdir -p "$APPDIR/usr/bin" "$APPDIR/usr/share/applications" \
         "$APPDIR/usr/share/icons/hicolor/512x512/apps"
cp build-appimage/bazarish-app "$APPDIR/usr/bin/"
cp app/icon/bazarish.png "$APPDIR/usr/share/icons/hicolor/512x512/apps/bazarish.png"
cat > "$APPDIR/usr/share/applications/bazarish.desktop" <<'DESKTOP'
[Desktop Entry]
Type=Application
Name=Bazarish
Comment=I2P messenger
Exec=bazarish-app
Icon=bazarish
Categories=Network;InstantMessaging;
Terminal=false
DESKTOP

mkdir -p /work
cd /work
cp /tools/linuxdeploy-x86_64.AppImage /tools/linuxdeploy-plugin-qt-x86_64.AppImage .
chmod +x linuxdeploy-x86_64.AppImage linuxdeploy-plugin-qt-x86_64.AppImage

export QML_SOURCES_PATHS=/src/app/qml
export QMAKE="$QTDIR/bin/qmake"
export LD_LIBRARY_PATH="$QTDIR/lib:/opt/openssl/lib64:${LD_LIBRARY_PATH:-}"
export EXTRA_QT_MODULES="multimedia"
# The C++ runtime travels too: the binary is compiled by GCC 13 and a host with
# an older one would refuse it.
./linuxdeploy-x86_64.AppImage --appdir "$APPDIR" \
  -e "$APPDIR/usr/bin/bazarish-app" \
  -d "$APPDIR/usr/share/applications/bazarish.desktop" \
  -i "$APPDIR/usr/share/icons/hicolor/512x512/apps/bazarish.png" \
  -l /usr/lib/x86_64-linux-gnu/libstdc++.so.6 \
  --plugin qt --output appimage

mv /work/Bazarish*.AppImage /out/
chmod 0777 /out/Bazarish*.AppImage
ls -la /out
