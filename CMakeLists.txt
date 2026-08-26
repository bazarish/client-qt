# Bazarish project (c) 2026
cmake_minimum_required(VERSION 3.20)
project(bazarish-client-qt6 CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/common/CMakeLists.txt")
    set(_bazarish_common_default "${CMAKE_CURRENT_SOURCE_DIR}/common")
else()
    set(_bazarish_common_default "${CMAKE_CURRENT_SOURCE_DIR}/../common")
endif()
set(BAZARISH_COMMON_DIR "${_bazarish_common_default}" CACHE PATH
    "Path to the bazarish common library repository")
if(NOT TARGET bazarish_common)
    add_subdirectory(${BAZARISH_COMMON_DIR} bazarish-common)
endif()

find_package(Threads REQUIRED)

# libqrencode renders contact invites as QR codes (multi-symbol structured
# append for the full self-verifying chain).
find_library(QRENCODE_LIBRARY NAMES qrencode REQUIRED)
find_path(QRENCODE_INCLUDE_DIR NAMES qrencode.h REQUIRED)

# libopus encodes call audio (48 kHz mono, 20 ms Opus frames). The media rides
# I2P datagrams; this is only the codec, so it stays private to the core lib.
find_library(OPUS_LIBRARY NAMES opus REQUIRED)
find_path(OPUS_INCLUDE_DIR NAMES opus/opus.h REQUIRED)

# I2P key material + transport now come from common's bazarish::i2p (the embedded
# libi2pd), so the previously-vendored trimmed libi2pd "keys" lib and the i2pd
# submodule are gone. OpenSSL is still needed for the crypto and for the TLS
# client common's HTTP engine uses on a clearnet facade.
# 3.5 is where ML-DSA and ML-KEM arrived (every signature and sealed envelope is
# hybrid) and 3.2 is where Argon2id did (an account key is Argon2id): an older
# OpenSSL builds a client that can neither create an account nor speak the
# protocol, so it is refused here rather than at the first passphrase.
# Distributions that ship an older one (Debian 12 has 3.0) need one built
# alongside: -DOPENSSL_ROOT_DIR=/path/to/openssl-3.5.
find_package(OpenSSL 3.5 REQUIRED)

# Sanitizer build (address / undefined), for chasing a crash rather than for
# shipping: BAZARISH_SANITIZE=address. It also drops -Werror, because under a
# sanitizer GCC raises maybe-uninitialized inside libstdc++ headers.
set(BAZARISH_SANITIZE "" CACHE STRING "Build with a sanitizer: address | undefined | address,undefined")
if(BAZARISH_SANITIZE)
    add_compile_options(-fsanitize=${BAZARISH_SANITIZE} -fno-omit-frame-pointer -g -Wno-error)
    add_link_options(-fsanitize=${BAZARISH_SANITIZE})
endif()

# The account store is SQLCipher (an encrypted SQLite): one file per account,
# unreadable without its key. It is a fork of SQLite exporting the same symbols,
# so nothing in this project may link plain libsqlite3 next to it.
find_library(SQLCIPHER_LIBRARY NAMES sqlcipher REQUIRED)
find_path(SQLCIPHER_INCLUDE_DIR NAMES sqlcipher/sqlite3.h REQUIRED)

add_library(bazarish_client_lib STATIC
    src/ApiClient.cpp
    src/AudioCodec.cpp
    src/AudioIo.cpp
    src/Bot.cpp
    src/Client.cpp
    src/CallMedia.cpp
    src/FederationFetch.cpp
    src/I2pKeys.cpp
    src/I2pRouter.cpp
    src/Invite.cpp
    src/FileTransfer.cpp
    src/OutboundCourier.cpp
    src/OutboundLeases.cpp
    src/AccountDb.cpp
    src/AccountKey.cpp
    src/AccountManager.cpp
    src/Qr.cpp
    src/Session.cpp
    src/WarmDestPool.cpp
)
target_include_directories(bazarish_client_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_include_directories(bazarish_client_lib PRIVATE ${QRENCODE_INCLUDE_DIR} ${OPUS_INCLUDE_DIR})
target_link_libraries(bazarish_client_lib PUBLIC Bazarish::Common Threads::Threads
    ${SQLCIPHER_LIBRARY})
target_link_libraries(bazarish_client_lib PRIVATE ${QRENCODE_LIBRARY} ${OPUS_LIBRARY})
target_compile_options(bazarish_client_lib PRIVATE -Wall -Wextra -Werror)
add_executable(bazarish-client src/Main.cpp)
target_link_libraries(bazarish-client PRIVATE bazarish_client_lib)
target_compile_options(bazarish-client PRIVATE -Wall -Wextra -Werror)

# A demo chat bot over the same Session logic: commands and inline keyboards.
add_executable(bazarish-bot src/BotMain.cpp)
target_link_libraries(bazarish-bot PRIVATE bazarish_client_lib)
target_compile_options(bazarish-bot PRIVATE -Wall -Wextra -Werror)

# The Qt6 Quick (QML) desktop app - the primary front-end over the same
# Session logic the CLI uses. Built only when Qt6 Quick is available.
# SVG (the brand logo) is rendered by Qt's runtime image-format plugin
# (imageformats/libqsvg), so we do NOT require the Qt6::Svg CMake component here -
# requiring it would fail the whole Qt6 find on systems that ship the plugin but
# not the Svg dev config, and silently skip the GUI target.
find_package(Qt6 COMPONENTS Quick QuickControls2 Qml Gui Multimedia Widgets Network QUIET)
if(Qt6_FOUND)
    qt_standard_project_setup(REQUIRES 6.5)

    # Optional: revealing a saved file selected in the system file manager uses the
    # freedesktop.org FileManager1 D-Bus interface on Linux. Found separately (not a
    # required component) so a system without the DBus dev config still builds the
    # GUI; the code falls back to opening the containing folder when it is absent.
    find_package(Qt6 COMPONENTS DBus QUIET)

    # The transcript store talks to SQLCipher's C API directly rather than through
    # Qt's QSQLITE driver: that driver links plain libsqlite3, and two SQLite
    # implementations exporting the same symbols must not meet in one process.

    qt_add_executable(bazarish-app
        app/main.cpp
        app/AppController.cpp
        app/AppController.hpp
        app/I2pController.cpp
        app/I2pController.hpp
        app/SessionController.cpp
        app/SessionController.hpp
        app/Models.cpp
        app/Models.hpp
        app/TranscriptStore.cpp
        app/TranscriptStore.hpp
        app/QtAudioIo.cpp
        app/QtAudioIo.hpp
        app/CallTones.cpp
        app/CallTones.hpp
        app/DesktopEntry.cpp
        app/DesktopEntry.hpp
        app/SingleInstance.cpp
        app/SingleInstance.hpp
        app/NotifySound.cpp
        app/NotifySound.hpp
        app/TrayIcon.cpp
        app/TrayIcon.hpp
        app/Identicon.cpp
        app/Identicon.hpp
        app/AvatarStore.cpp
        app/PictureStore.cpp
        app/VoiceNote.cpp
        app/AvatarStore.hpp
    )
    target_include_directories(bazarish-app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(bazarish-app PRIVATE
        bazarish_client_lib ${QRENCODE_LIBRARY} ${SQLCIPHER_LIBRARY}
        Qt6::Quick Qt6::QuickControls2 Qt6::Qml Qt6::Gui Qt6::Multimedia Qt6::Widgets
        Qt6::Network)
    target_include_directories(bazarish-app PRIVATE ${QRENCODE_INCLUDE_DIR} ${SQLITE3_INCLUDE_DIR})
    target_compile_options(bazarish-app PRIVATE -Wall -Wextra)

    # Reveal-in-file-manager (FileManager1 over D-Bus) when the optional module is
    # present; otherwise the code path is compiled out and falls back gracefully.
    if(TARGET Qt6::DBus)
        target_link_libraries(bazarish-app PRIVATE Qt6::DBus)
        target_compile_definitions(bazarish-app PRIVATE BAZARISH_HAVE_QTDBUS)
    endif()

    # Bundle the brand font (Roboto Mono, static Latin cuts) so the terminal/neon
    # look renders identically regardless of what is installed; non-Latin text
    # falls back to the system monospace.
    qt_add_resources(bazarish-app appfonts
        PREFIX "/fonts"
        BASE app/fonts
        FILES
            app/fonts/RobotoMono-Regular.ttf
            app/fonts/RobotoMono-Bold.ttf
            app/fonts/TwemojiMozilla.ttf
    )

    # The notification sound, carried rather than looked for: a system sound theme
    # is not something this application can count on being installed.
    qt_add_resources(bazarish-app appsound
        PREFIX "/sound"
        BASE app/sound
        FILES
            app/sound/notify.wav
    )

    # The brand app icon (rendered from the brand kit's icon.svg) and the brand
    # wordmark logo as SVG (scales crisply at any size / DPI).
    qt_add_resources(bazarish-app appicon
        PREFIX "/icon"
        BASE app/icon
        FILES
            app/icon/bazarish.png
            app/icon/logo.svg
    )

    set_source_files_properties(app/qml/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
    qt_add_qml_module(bazarish-app
        URI Bazarish
        VERSION 1.0
        # Said rather than left to the default, which moved in Qt 6.5: the engine
        # is handed one URL, and it has to name the same place on either version.
        RESOURCE_PREFIX "/qt/qml"
        QML_FILES
            app/qml/Main.qml
            app/qml/Theme.qml
            app/qml/DeliveryStatus.qml
            app/qml/AccountPicker.qml
            app/qml/CreateAccountWizard.qml
            app/qml/ConnectServerPage.qml
            app/qml/MainView.qml
            app/qml/ChatList.qml
            app/qml/AccountSwitcher.qml
            app/qml/ConversationView.qml
            app/qml/ImageViewer.qml
            app/qml/MessageBubble.qml
            app/qml/SearchPopup.qml
            app/qml/ReactionPicker.qml
            app/qml/ForwardSheet.qml
            app/qml/Composer.qml
            app/qml/FileSendDialog.qml
            app/qml/VoiceRecorder.qml
            app/qml/NewChatSheet.qml
            app/qml/InviteSheet.qml
            app/qml/SignWithKeySheet.qml
            app/qml/ContactInfo.qml
            app/qml/ContactShareSheet.qml
            app/qml/CallScreen.qml
            app/qml/AppSettingsPage.qml
            app/qml/SettingsPage.qml
            app/qml/RouterStatusPage.qml
            app/qml/ServerHelloDialog.qml
            app/qml/AccountDeleteConfirmDialog.qml
            app/qml/AccountDeleteLockedDialog.qml
            app/qml/AccountDeleteFailedDialog.qml
            app/qml/OperationsOverlay.qml
            app/qml/OperationRow.qml
            app/qml/components/Avatar.qml
            app/qml/components/AvatarViewer.qml
            app/qml/components/QrView.qml
            app/qml/components/AvatarCropDialog.qml
            app/qml/components/Toggle.qml
            app/qml/components/ContextMenu.qml
            app/qml/components/ContextMenuItem.qml
            app/qml/components/StatusChip.qml
            app/qml/components/UnreadBadge.qml
            app/qml/components/DialogButtons.qml
            app/qml/components/Icon.qml
            app/qml/components/IconButton.qml
            app/qml/components/MenuButton.qml
            app/qml/components/FormField.qml
            app/qml/components/ServerConnectForm.qml
            app/qml/components/ConnectProgressDialog.qml
    )
    message(STATUS "Qt6 Quick found: building bazarish-app")
else()
    message(STATUS "Qt6 Quick not found: skipping bazarish-app")
endif()

enable_testing()

function(bazarish_client_add_test name)
    add_executable(${name} tests/${name}.cpp)
    target_link_libraries(${name} PRIVATE bazarish_client_lib)
    target_compile_options(${name} PRIVATE -Wall -Wextra -Werror)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

bazarish_client_add_test(TestApiClient)
bazarish_client_add_test(TestBot)
bazarish_client_add_test(TestCallMedia)
bazarish_client_add_test(TestFacade)
bazarish_client_add_test(TestClient)
bazarish_client_add_test(TestTokenRefill)
bazarish_client_add_test(TestFileTransfer)
bazarish_client_add_test(TestOutboundCourier)
bazarish_client_add_test(TestI2pKeys)
bazarish_client_add_test(TestInvite)
bazarish_client_add_test(TestLogin)
bazarish_client_add_test(TestAccount)

# The transcript store needs Qt SQL and direct SQLite, so it has its own target.
if(Qt6_FOUND)
    add_executable(TestTranscript tests/TestTranscript.cpp app/TranscriptStore.cpp)
    target_include_directories(TestTranscript PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/app ${SQLITE3_INCLUDE_DIR})
    target_link_libraries(TestTranscript PRIVATE
        bazarish_client_lib ${SQLCIPHER_LIBRARY} Qt6::Core)
    target_compile_options(TestTranscript PRIVATE -Wall -Wextra)
    add_test(NAME TestTranscript COMMAND TestTranscript)
endif()
