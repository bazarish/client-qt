# Bazarish - client-qt

The desktop client of the [Bazarish](https://github.com/bazarish/docs-main)
messenger: a Qt6 Quick application, `bazarish-app`, over the client core library.

The core - the API transport, the typed endpoints, and the session that holds the
identity, the contacts, the mailbox sync, calls and file transfer - is shared
with the other implementations through the `common` submodule. Headless use is
not this repository's job: a bot or a scripted account is `bazarish-bot`, which
runs the same core without an interface.

## Documentation

[`docs/`](docs/README.md) documents this client: what it settles where the
protocol leaves a choice, and the limits of this implementation. The protocol
itself - wire formats, delivery states, admission rules - is specified in
[`docs-main`](https://github.com/bazarish/docs-main) and is common to every
client.

## Build

CMake >= 3.20, C++20, OpenSSL >= 3.5, **SQLCipher 4**, Opus, libqrencode and
Boost. Qt6 (Quick, QuickControls2, Qml, Gui, Multimedia, Widgets, Network) is
what builds the application; without it only the core library and its tests are
built. `common` is a submodule.

SQLCipher 3 links and then cannot read an account: the version-4 database format
is not the one it writes, and there is no compatibility pragma from that side
([docs/AccountStorage.md](docs/AccountStorage.md)).

```bash
git clone --recurse-submodules https://github.com/bazarish/client-qt
cmake -S . -B build && cmake --build build -j4
ctest --test-dir build
```

The version the application shows is stamped on every build from the git tag and
the short commit; a tree without git reads `dev`.

`packaging/appimage-build.sh` builds the portable Linux AppImage, x86_64 on a Debian 12
base and aarch64 on an Ubuntu 24.04 one, carrying its own Qt, OpenSSL and SQLCipher.

`packaging/android/build.sh <output dir>` builds the Android package for arm64-v8a
(Android 9 and newer) and signs it with the keystore it is given. It takes the
toolchain from the environment: `ANDROID_SDK_ROOT`, `ANDROID_NDK_ROOT` (NDK r27c),
`ANDROID_BUILD_TOOLS`, `QT_ANDROID_ROOT` and `QT_HOST_ROOT` (Qt 6.8.3 for Android and
for the desktop), `JAVA_HOME` (a JDK 17), and `BAZARISH_ANDROID_KEYSTORE`,
`BAZARISH_ANDROID_KEY_ALIAS`, `BAZARISH_ANDROID_KEY_PASS`. The release workflow
runs the same script.
