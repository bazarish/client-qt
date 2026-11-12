#!/bin/bash
# Runs on its build base and newer: Debian 12 for x86_64, Ubuntu 24.04 for aarch64.
set -eux
export DEBIAN_FRONTEND=noninteractive
export APPIMAGE_EXTRACT_AND_RUN=1
# A build that scales to the host starves the desktop it runs beside.
readonly kJobs=4

case "$(uname -m)" in
x86_64)
    readonly kQtHost=linux kQtArch=linux_gcc_64 kQtDir=gcc_64 kDeploy=x86_64
    readonly kAlsa=libasound2 kGstPhotography=libgstreamer-plugins-bad1.0-0
    ;;
aarch64)
    readonly kQtHost=linux_arm64 kQtArch=linux_gcc_arm64 kQtDir=gcc_arm64 kDeploy=aarch64
    readonly kAlsa=libasound2t64 kGstPhotography=libgstreamer-plugins-good1.0-0
    ;;
*)
    echo "no AppImage for $(uname -m)" >&2
    exit 1
    ;;
esac

apt-get update -qq
# qt6-*-dev only pulls in Qt's system libraries; GStreamer 1.24 moved gstphotography to -good.
apt-get install -y -qq --no-install-recommends \
  build-essential cmake git ca-certificates wget file rsync patchelf python3-pip tcl \
  libssl-dev zlib1g-dev libsqlcipher-dev libopus-dev libqrencode-dev libbrotli-dev \
  libboost-dev libboost-program-options-dev \
  qt6-base-dev qt6-declarative-dev qt6-multimedia-dev libgl-dev libxkbcommon-dev \
  libxcb-cursor0 libxcb-icccm4 libxcb-image0 libxcb-keysyms1 libxcb-randr0 \
  libxcb-render-util0 libxcb-shape0 libxcb-xinerama0 libxcb-xkb1 libxkbcommon-x11-0 \
  libwayland-client0 libwayland-cursor0 libwayland-egl1 \
  libxrandr2 libxext6 libxfixes3 libxdamage1 libxi6 libxtst6 libxrender1 libsm6 libice6 \
  libva2 libva-drm2 libva-x11-2 libvdpau1 libdrm2 "$kAlsa" libpulse0 libsndfile1 \
  libgstreamer-plugins-bad1.0-0 libgstreamer-gl1.0-0 "$kGstPhotography"

QTDIR="/opt/Qt/6.8.2/$kQtDir"
if [ ! -x "$QTDIR/bin/qmake" ]; then
    pip install --break-system-packages -q aqtinstall
    aqt install-qt "$kQtHost" desktop 6.8.2 "$kQtArch" \
        -m qtmultimedia qtshadertools -O /opt/Qt
fi
test -x "$QTDIR/bin/qmake"

# The base's OpenSSL may predate Argon2id (3.2+), which an account key needs.
OPENSSL_VERSION=3.5.7
if [ ! -f /opt/openssl/lib/libcrypto.so.3 ]; then
    cd /work-openssl 2>/dev/null || { mkdir -p /work-openssl; cd /work-openssl; }
    wget -q "https://github.com/openssl/openssl/releases/download/openssl-${OPENSSL_VERSION}/openssl-${OPENSSL_VERSION}.tar.gz"
    tar xf "openssl-${OPENSSL_VERSION}.tar.gz"
    cd "openssl-${OPENSSL_VERSION}"
    ./Configure --prefix=/opt/openssl --openssldir=/opt/openssl/ssl --libdir=lib shared no-docs
    make -j"$kJobs"
    make install_sw
fi
export LD_LIBRARY_PATH=/opt/openssl/lib:${LD_LIBRARY_PATH:-}

# The base's SQLCipher 3 cannot read an account a SQLCipher 4 build wrote.
SQLCIPHER_VERSION=4.6.1
if [ ! -f /opt/sqlcipher/lib/libsqlcipher.so ]; then
    cd /work-sqlcipher 2>/dev/null || { mkdir -p /work-sqlcipher; cd /work-sqlcipher; }
    # Ubuntu's wget is linked against OpenSSL, and the one built above has no CA store.
    env -u LD_LIBRARY_PATH wget -q -O "sqlcipher-${SQLCIPHER_VERSION}.tar.gz" \
        "https://github.com/sqlcipher/sqlcipher/archive/refs/tags/v${SQLCIPHER_VERSION}.tar.gz"
    tar xf "sqlcipher-${SQLCIPHER_VERSION}.tar.gz"
    cd "sqlcipher-${SQLCIPHER_VERSION}"
    ./configure --prefix=/opt/sqlcipher --enable-tempstore=yes --disable-tcl \
        CFLAGS="-DSQLITE_HAS_CODEC -I/opt/openssl/include" \
        LDFLAGS="-L/opt/openssl/lib -lcrypto"
    make -j"$kJobs"
    make install
fi
export LD_LIBRARY_PATH=/opt/sqlcipher/lib:${LD_LIBRARY_PATH}

# --delete drops files the repository no longer has; the excludes are the receiver's own.
rsync -a --delete --exclude 'build' --exclude 'build-asan' --exclude 'build-appimage' \
    --exclude 'AppDir' /host/ /src/
cd /src
git config --global --add safe.directory '*'

# -U drops cached OpenSSL paths, which OPENSSL_ROOT_DIR would not override.
cmake -S . -B build-appimage -DCMAKE_BUILD_TYPE=Release -UOPENSSL_* -USQLCIPHER_* \
    -DCMAKE_PREFIX_PATH="$QTDIR" -DOPENSSL_ROOT_DIR=/opt/openssl \
    -DSQLCIPHER_LIBRARY=/opt/sqlcipher/lib/libsqlcipher.so \
    -DSQLCIPHER_INCLUDE_DIR=/opt/sqlcipher/include
cmake --build build-appimage -j"$kJobs" --target bazarish-app

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
# Qt reports the application name as the WM class; a shell matches it to this entry.
StartupWMClass=Bazarish
DESKTOP

mkdir -p /work
cd /work
cp "/tools/linuxdeploy-$kDeploy.AppImage" "/tools/linuxdeploy-plugin-qt-$kDeploy.AppImage" .
chmod +x "linuxdeploy-$kDeploy.AppImage" "linuxdeploy-plugin-qt-$kDeploy.AppImage"

export QML_SOURCES_PATHS=/src/app/qml
export QMAKE="$QTDIR/bin/qmake"
export LD_LIBRARY_PATH="$QTDIR/lib:/opt/openssl/lib:${LD_LIBRARY_PATH:-}"
export EXTRA_QT_MODULES="multimedia"
# libxcb-glx belongs to the host's GL stack: an older bundled copy breaks GLX on a newer host.
"./linuxdeploy-$kDeploy.AppImage" --appdir "$APPDIR" \
  -e "$APPDIR/usr/bin/bazarish-app" \
  -d "$APPDIR/usr/share/applications/bazarish.desktop" \
  -i "$APPDIR/usr/share/icons/hicolor/512x512/apps/bazarish.png" \
  --exclude-library "libxcb-glx.so*" \
  --plugin qt --output appimage

mv /work/Bazarish*.AppImage /out/
chmod 0777 /out/Bazarish*.AppImage
ls -la /out
