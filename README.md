# Bazarish - client-qt

The desktop client of the [Bazarish](https://github.com/bazarish/docs-main)
messenger: a Qt6 Quick application, `bazarish-app`, over the client core library.

The core - the API transport, the typed endpoints, and the session that holds the
identity, the contacts, the mailbox sync, calls and file transfer - is shared
with the other implementations through the `common` submodule. Headless use - a
bot or a scripted account - is `bazarish-bot`, which runs the same core without
an interface.

## Documentation

[`docs/`](docs/README.md) documents this client: what it settles where the
protocol leaves a choice, and the limits of this implementation. The protocol
itself - wire formats, delivery states, admission rules - is specified in
[`docs-main`](https://github.com/bazarish/docs-main) and is common to every
client.

## Build

CMake >= 3.20, C++20, a C compiler and assembler, OpenSSL >= 3.5, **SQLCipher 4**,
Opus, libqrencode, Boost and brotli (the decoder). Qt6 (Quick, QuickControls2, Qml,
Gui, Multimedia, Widgets, Network) is what builds the application; without it only
the core library and its tests are built. `common` is a submodule, and BoringSSL is
a submodule of `common`: it is built with the client, for the TLS of a private
gateway's link alone ([docs/I2pRouter.md](docs/I2pRouter.md)). Everything else
uses OpenSSL.

On x86-64 Windows BoringSSL's assembly is NASM's, so the build there also needs
NASM.

An account is in the SQLCipher version-4 database format, which SQLCipher 3
does not read ([docs/AccountStorage.md](docs/AccountStorage.md)).

```bash
git clone --recurse-submodules https://github.com/bazarish/client-qt
cmake -S . -B build && cmake --build build -j4
ctest --test-dir build
```

The version the application shows is stamped on every build from git: the tag on
a tagged commit, `dev (<short hash>)` on any other. A tree without git does not
build.

`packaging/appimage-build.sh` builds the portable Linux AppImage, x86_64 on a Debian 12
base and aarch64 on an Ubuntu 24.04 one, carrying its own Qt, OpenSSL and SQLCipher.

`packaging/windows/build.sh <x86_64|aarch64> <work dir> <output zip>` cross-builds the
portable Windows folder on a Linux host, x86_64 or aarch64: llvm-mingw with the UCRT,
the MSYS2 CLANG64 or CLANGARM64 packages and the host's Qt tools of the same version,
all fetched into the work dir and kept there for the next build. It needs python3 with
aqtinstall, cmake, ninja, zip and zstd, and nasm for an x86_64 target.

`packaging/android/build.sh <output dir>` builds the Android package for arm64-v8a
(Android 12 and newer) as `Bazarish-android-arm64.apk`, zipaligned and unsigned: the
release signs it with its own key. It takes the toolchain from the environment:
`ANDROID_SDK_ROOT`, `ANDROID_NDK_ROOT` (NDK r27c), `ANDROID_BUILD_TOOLS`,
`QT_ANDROID_ROOT` and `QT_HOST_ROOT` (Qt 6.8.3 for Android and for the desktop) and
`JAVA_HOME` (a JDK 17).
