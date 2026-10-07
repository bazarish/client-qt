#!/bin/sh
# Bazarish project (c) 2026
# A client with nothing to install beside it: musl, static Qt, static everything.
# Runs inside Alpine; the sources arrive on /host and the result stays on /out,
# so an interrupted run carries on where it stopped.
set -eu

readonly kJobs=4
readonly kPrefix=/out/prefix
readonly kQt=/out/qt
readonly kWork=/out/work
readonly kOpenSsl=3.5.7
readonly kQtVersion=6.8.2
readonly kSqlCipher=4.6.1
readonly kOpus=1.5.2
readonly kQrencode=4.1.1
readonly kAlsa=1.2.12
readonly kHarfbuzz=14.2.1
readonly kXcbUtil=0.4.1
readonly kXau=1.0.12
readonly kX11=1.8.12
readonly kQtMirror=https://download.qt.io/archive/qt/6.8/6.8.2/submodules

mkdir -p "$kPrefix" "$kQt" "$kWork"
export PKG_CONFIG_PATH="$kPrefix/lib/pkgconfig"
# Read by static-only.cmake, which names what a static library needs beside it.
export BAZARISH_PREFIX="$kPrefix"
export PATH="$kPrefix/bin:$PATH"

step() {
    if [ -f "$kPrefix/.$1" ]; then
        echo "== $1 already built"
        return 1
    fi
    echo "== $1"
    return 0
}

done_with() {
    touch "$kPrefix/.$1"
}

# Downloaded aside and moved into place, so an interrupted run never leaves a
# half file that looks finished.
fetch() {
    test -f "$kWork/$2" && return 0
    wget -q -O "$kWork/$2.part" "$1"
    mv "$kWork/$2.part" "$kWork/$2"
}

# The flags are the ones a portable static build needs: no shared objects, no
# ucontext (musl has none that OpenSSL's async engine can use), and no reading of
# a host openssl.cnf that belongs to a different build.
if step openssl; then
    fetch "https://github.com/openssl/openssl/releases/download/openssl-$kOpenSsl/openssl-$kOpenSsl.tar.gz" \
        "openssl-$kOpenSsl.tar.gz"
    cd "$kWork" && rm -rf "openssl-$kOpenSsl" && tar xf "openssl-$kOpenSsl.tar.gz"
    cd "openssl-$kOpenSsl"
    # Everything under one libdir: on x86_64 OpenSSL would otherwise install into
    # lib64, where nothing else here looks.
    ./Configure linux-x86_64 no-shared no-async no-autoload-config no-docs \
        --prefix="$kPrefix" --openssldir="$kPrefix/ssl" --libdir=lib
    make -j"$kJobs"
    make install_sw
    cd "$kWork" && rm -rf "openssl-$kOpenSsl"
    done_with openssl
fi

if step opus; then
    fetch "https://downloads.xiph.org/releases/opus/opus-$kOpus.tar.gz" "opus-$kOpus.tar.gz"
    cd "$kWork" && rm -rf "opus-$kOpus" && tar xf "opus-$kOpus.tar.gz" && cd "opus-$kOpus"
    ./configure --prefix="$kPrefix" --enable-static --disable-shared --disable-doc
    make -j"$kJobs"
    make install
    cd "$kWork" && rm -rf "opus-$kOpus"
    done_with opus
fi

if step qrencode; then
    fetch "https://github.com/fukuchi/libqrencode/archive/refs/tags/v$kQrencode.tar.gz" \
        "qrencode-$kQrencode.tar.gz"
    cd "$kWork" && rm -rf "libqrencode-$kQrencode" && tar xf "qrencode-$kQrencode.tar.gz"
    cd "libqrencode-$kQrencode"
    # Its CMakeLists asks for a policy version this CMake no longer accepts.
    cmake -S . -B build -G Ninja -DCMAKE_INSTALL_PREFIX="$kPrefix" \
        -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
        -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF -DWITH_TOOLS=NO -DWITH_TESTS=NO
    cmake --build build -j "$kJobs"
    cmake --install build
    cd "$kWork" && rm -rf "libqrencode-$kQrencode"
    done_with qrencode
fi

if step sqlcipher; then
    fetch "https://github.com/sqlcipher/sqlcipher/archive/refs/tags/v$kSqlCipher.tar.gz" \
        "sqlcipher-$kSqlCipher.tar.gz"
    cd "$kWork" && rm -rf "sqlcipher-$kSqlCipher" && tar xf "sqlcipher-$kSqlCipher.tar.gz"
    cd "sqlcipher-$kSqlCipher"
    ./configure --prefix="$kPrefix" --enable-static --disable-shared --disable-tcl \
        --enable-tempstore=yes \
        CFLAGS="-DSQLITE_HAS_CODEC -I$kPrefix/include" \
        LDFLAGS="-L$kPrefix/lib" LIBS="-lcrypto"
    make -j"$kJobs"
    make install
    cd "$kWork" && rm -rf "sqlcipher-$kSqlCipher"
    done_with sqlcipher
fi

# Audio reaches the host through ALSA: a static process cannot load the host's
# PulseAudio client library, which is glibc's.
if step alsa; then
    fetch "https://www.alsa-project.org/files/pub/lib/alsa-lib-$kAlsa.tar.bz2" \
        "alsa-lib-$kAlsa.tar.bz2"
    cd "$kWork" && rm -rf "alsa-lib-$kAlsa" && tar xf "alsa-lib-$kAlsa.tar.bz2"
    cd "alsa-lib-$kAlsa"
    ./configure --prefix="$kPrefix" --enable-static --disable-shared --disable-python
    make -j"$kJobs"
    make install
    cd "$kWork" && rm -rf "alsa-lib-$kAlsa"
    done_with alsa
fi

# Qt takes longer than any one sitting, so each module keeps its source and its
# build directory until it is installed: a build that was cut short carries on.
# The distribution's harfbuzz is built against glib, which it then needs on the
# link line, and of glib, libintl and pcre2 Alpine has no static library at all.
# Qt asks harfbuzz for shaping and nothing else, so the dependency goes rather
# than three more builds. The version matches the one Qt was compiled against.
if step harfbuzz; then
    fetch "https://github.com/harfbuzz/harfbuzz/releases/download/$kHarfbuzz/harfbuzz-$kHarfbuzz.tar.xz" \
        "harfbuzz-$kHarfbuzz.tar.xz"
    cd "$kWork" && rm -rf "harfbuzz-$kHarfbuzz" && tar xf "harfbuzz-$kHarfbuzz.tar.xz"
    cd "harfbuzz-$kHarfbuzz"
    meson setup build --prefix="$kPrefix" --default-library=static --buildtype=release \
        -Dglib=disabled -Dgobject=disabled -Dicu=disabled -Dcairo=disabled \
        -Dchafa=disabled -Dgraphite=disabled -Dtests=disabled -Ddocs=disabled \
        -Dutilities=disabled -Dfreetype=enabled
    meson compile -C build -j "$kJobs"
    meson install -C build
    cd "$kWork" && rm -rf "harfbuzz-$kHarfbuzz"
    done_with harfbuzz
fi

# The platform plugin calls into xcb-aux, which lives in xcb-util, and that one
# Alpine ships shared only.
if step xcbutil; then
    fetch "https://xcb.freedesktop.org/dist/xcb-util-$kXcbUtil.tar.xz" \
        "xcb-util-$kXcbUtil.tar.xz"
    cd "$kWork" && rm -rf "xcb-util-$kXcbUtil" && tar xf "xcb-util-$kXcbUtil.tar.xz"
    cd "xcb-util-$kXcbUtil"
    ./configure --prefix="$kPrefix" --enable-static --disable-shared
    make -j"$kJobs"
    make install
    cd "$kWork" && rm -rf "xcb-util-$kXcbUtil"
    done_with xcbutil
fi

# Alpine ships these two shared only, and the xcb platform plugin needs both,
# so they are built here like everything else the binary must carry.
if step x11; then
    fetch "https://www.x.org/releases/individual/lib/libXau-$kXau.tar.xz" "libXau-$kXau.tar.xz"
    cd "$kWork" && rm -rf "libXau-$kXau" && tar xf "libXau-$kXau.tar.xz"
    cd "libXau-$kXau"
    ./configure --prefix="$kPrefix" --enable-static --disable-shared
    make -j"$kJobs"
    make install
    cd "$kWork" && rm -rf "libXau-$kXau"

    fetch "https://www.x.org/releases/individual/lib/libX11-$kX11.tar.xz" "libX11-$kX11.tar.xz"
    cd "$kWork" && rm -rf "libX11-$kX11" && tar xf "libX11-$kX11.tar.xz"
    cd "libX11-$kX11"
    ./configure --prefix="$kPrefix" --enable-static --disable-shared --disable-specs \
        --without-xmlto --without-fop --without-xsltproc
    make -j"$kJobs"
    make install
    cd "$kWork" && rm -rf "libX11-$kX11"
    done_with x11
fi

qt_module() {
    name=$1
    if ! step "qt-$name"; then
        return 0
    fi
    fetch "$kQtMirror/$name-everywhere-src-$kQtVersion.tar.xz" "$name-$kQtVersion.tar.xz"
    source="$kWork/$name-everywhere-src-$kQtVersion"
    build="$kWork/build-$name"
    test -d "$source" || tar -C "$kWork" -xf "$kWork/$name-$kQtVersion.tar.xz"
    mkdir -p "$build"
    cd "$build"
    if [ ! -f CMakeCache.txt ]; then
        if [ "$name" = qtbase ]; then
            "$source/configure" -static -release -prefix "$kQt" \
                -opensource -confirm-license -nomake examples -nomake tests \
                -no-opengl -no-dbus -no-icu -no-glib -qt-pcre \
                -system-zlib -system-freetype -system-harfbuzz -system-libpng \
                -system-libjpeg -fontconfig -xcb -openssl-linked \
                -- -DOPENSSL_ROOT_DIR="$kPrefix" -DCMAKE_PREFIX_PATH="$kPrefix"
        else
            # glslang, bundled in qtshadertools, names uint32_t without including
            # <cstdint>, which compiles only where another header drags it in.
            "$kQt/bin/qt-configure-module" "$source" -- \
                -DCMAKE_PREFIX_PATH="$kPrefix" -DCMAKE_CXX_FLAGS="-include cstdint"
        fi
    fi
    cmake --build . --parallel "$kJobs"
    cmake --install .
    done_with "qt-$name"
    rm -rf "$build" "$source"
}

qt_module qtbase
qt_module qtshadertools
qt_module qtdeclarative
qt_module qtmultimedia
qt_module qtsvg

if step app; then
    rm -rf /out/build
    cmake -S /host -B /out/build -G Ninja -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_PROJECT_INCLUDE=/host/packaging/musl/static-only.cmake \
        -DCMAKE_PREFIX_PATH="$kQt;$kPrefix" \
        -DOPENSSL_ROOT_DIR="$kPrefix" \
        -DSQLCIPHER_LIBRARY="$kPrefix/lib/libsqlcipher.a" \
        -DSQLCIPHER_INCLUDE_DIR="$kPrefix/include" \
        -DCMAKE_EXE_LINKER_FLAGS="-static -lexpat"
    cmake --build /out/build -j"$kJobs" --target bazarish-app
    # A static Qt carries a great deal of debug information into the binary.
    strip /out/build/bazarish-app
    done_with app
fi

echo "== what came out"
file /out/build/bazarish-app
ldd /out/build/bazarish-app 2>&1 | head -2 || true
ls -la /out/build/bazarish-app
