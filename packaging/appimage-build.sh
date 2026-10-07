#!/bin/bash
# Portable Bazarish AppImage. Built on Debian 12 (glibc 2.36) against an upstream
# Qt 6.8: the oldest glibc worth supporting, with everything the host would
# otherwise have to provide - Qt, OpenSSL 3.5, SQLCipher - carried inside. A
# binary built here runs on Debian 12 and on anything newer; the reverse is not
# true, which is why the base is the older distribution and not the newer one.
set -eux
export DEBIAN_FRONTEND=noninteractive
export APPIMAGE_EXTRACT_AND_RUN=1
# A build that scales to the host starves the desktop it runs beside.
readonly kJobs=4

# The same recipe on either architecture: what differs is the name Qt and
# linuxdeploy give their builds.
case "$(uname -m)" in
    x86_64)
        readonly kQtHost=linux kQtArch=linux_gcc_64 kQtDir=gcc_64 kDeploy=x86_64
        ;;
    aarch64)
        readonly kQtHost=linux_arm64 kQtArch=linux_gcc_arm64 kQtDir=gcc_arm64 kDeploy=aarch64
        ;;
    *)
        echo "no AppImage recipe for $(uname -m)" >&2
        exit 1
        ;;
esac

apt-get update -qq
# qt6-*-dev is installed for the system libraries Qt itself needs (X, GL,
# fontconfig, audio); the build itself uses the Qt under /opt.
apt-get install -y -qq --no-install-recommends \
  build-essential cmake git ca-certificates wget file rsync patchelf python3-pip tcl \
  libssl-dev zlib1g-dev libsqlcipher-dev libopus-dev libqrencode-dev \
  libboost-dev libboost-program-options-dev \
  qt6-base-dev qt6-declarative-dev qt6-multimedia-dev libgl-dev libxkbcommon-dev \
  libxcb-cursor0 libxcb-icccm4 libxcb-image0 libxcb-keysyms1 libxcb-randr0 \
  libxcb-render-util0 libxcb-shape0 libxcb-xinerama0 libxcb-xkb1 libxkbcommon-x11-0 \
  libwayland-client0 libwayland-cursor0 libwayland-egl1 \
  libxrandr2 libxext6 libxfixes3 libxdamage1 libxi6 libxtst6 libxrender1 libsm6 libice6 \
  libva2 libva-drm2 libva-x11-2 libvdpau1 libdrm2 libasound2 libpulse0 libsndfile1

# Qt 6.8's own Linux binaries are built for glibc 2.28+, so they run on this base.
QTDIR="/opt/Qt/6.8.2/$kQtDir"
if [ ! -x "$QTDIR/bin/qmake" ]; then
    pip install --break-system-packages -q aqtinstall
    aqt install-qt "$kQtHost" desktop 6.8.2 "$kQtArch" \
        -m qtmultimedia qtshadertools -O /opt/Qt
fi
test -x "$QTDIR/bin/qmake"

# OpenSSL: 24.04 ships 3.0, which has no Argon2id (3.2+), and an account key is
# Argon2id. Built and bundled here so the host's version does not decide whether
# an account can be created at all.
OPENSSL_VERSION=3.5.7
if [ ! -f /opt/openssl/lib64/libcrypto.so.3 ]; then
    cd /work-openssl 2>/dev/null || { mkdir -p /work-openssl; cd /work-openssl; }
    wget -q "https://github.com/openssl/openssl/releases/download/openssl-${OPENSSL_VERSION}/openssl-${OPENSSL_VERSION}.tar.gz"
    tar xf "openssl-${OPENSSL_VERSION}.tar.gz"
    cd "openssl-${OPENSSL_VERSION}"
    ./Configure --prefix=/opt/openssl --openssldir=/opt/openssl/ssl shared no-docs
    make -j"$kJobs"
    make install_sw
fi
export LD_LIBRARY_PATH=/opt/openssl/lib64:${LD_LIBRARY_PATH:-}

# SQLCipher: this base ships 3.4.1, whose database format is not the one
# SQLCipher 4 writes - an account made by any build with a 4.x library is simply
# unreadable here, and the application can only report it as locked. Built from
# source against the OpenSSL above so the account file means the same thing
# wherever it was made.
SQLCIPHER_VERSION=4.6.1
if [ ! -f /opt/sqlcipher/lib/libsqlcipher.so ]; then
    cd /work-sqlcipher 2>/dev/null || { mkdir -p /work-sqlcipher; cd /work-sqlcipher; }
    wget -q -O "sqlcipher-${SQLCIPHER_VERSION}.tar.gz" \
        "https://github.com/sqlcipher/sqlcipher/archive/refs/tags/v${SQLCIPHER_VERSION}.tar.gz"
    tar xf "sqlcipher-${SQLCIPHER_VERSION}.tar.gz"
    cd "sqlcipher-${SQLCIPHER_VERSION}"
    ./configure --prefix=/opt/sqlcipher --enable-tempstore=yes --disable-tcl \
        CFLAGS="-DSQLITE_HAS_CODEC -I/opt/openssl/include" \
        LDFLAGS="-L/opt/openssl/lib64 -lcrypto"
    make -j"$kJobs"
    make install
fi
export LD_LIBRARY_PATH=/opt/sqlcipher/lib:${LD_LIBRARY_PATH}

# --delete, and not only a copy: this directory outlives one build, and a source
# file the repository no longer has must not stay behind in it. The client core
# moved out of src/ into common/, and the copies left there went on shadowing the
# headers that replaced them - a build that fails loudly if you are lucky.
# The build directory and what is made from it are the receiver's own and are
# excluded, which also protects them from the delete.
rsync -a --delete --exclude 'build' --exclude 'build-asan' --exclude 'build-appimage' \
    --exclude 'AppDir' /host/ /src/
cd /src
git config --global --add safe.directory '*'

# -U clears any OpenSSL paths a previous configure cached: the root below is
# only consulted when they are not already set.
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
# What the window says it is (Qt reports the application name as the WM class):
# a shell that maps windows to installed entries needs the two to match, and the
# instance name it would otherwise use is the AppImage's file name.
StartupWMClass=Bazarish
DESKTOP

mkdir -p /work
cd /work
cp "/tools/linuxdeploy-$kDeploy.AppImage" "/tools/linuxdeploy-plugin-qt-$kDeploy.AppImage" .
chmod +x "linuxdeploy-$kDeploy.AppImage" "linuxdeploy-plugin-qt-$kDeploy.AppImage"

export QML_SOURCES_PATHS=/src/app/qml
export QMAKE="$QTDIR/bin/qmake"
export LD_LIBRARY_PATH="$QTDIR/lib:/opt/openssl/lib64:${LD_LIBRARY_PATH:-}"
export EXTRA_QT_MODULES="multimedia"
# The C++ runtime is NOT bundled: this base has the oldest libstdc++ we support,
# so every host that can run this image already has one at least as new. Carrying
# it would put it ahead of the host's own on the library path, where the host's
# graphics stack picks it up too - and Mesa built against a newer libstdc++ then
# fails to load, which reads as "Could not initialize GLX" and no window at all.
# libxcb-glx belongs to the host's graphics stack, not to this bundle: carried
# along from an older base it is loaded next to the host's own libxcb, and GLX
# then fails to initialize on a newer system ("Could not initialize GLX").
"./linuxdeploy-$kDeploy.AppImage" --appdir "$APPDIR" \
  -e "$APPDIR/usr/bin/bazarish-app" \
  -d "$APPDIR/usr/share/applications/bazarish.desktop" \
  -i "$APPDIR/usr/share/icons/hicolor/512x512/apps/bazarish.png" \
  --exclude-library "libxcb-glx.so*" \
  --plugin qt --output appimage

mv /work/Bazarish*.AppImage /out/
chmod 0777 /out/Bazarish*.AppImage
ls -la /out
