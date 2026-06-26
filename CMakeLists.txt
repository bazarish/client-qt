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

# libvpx encodes call video (VP8, low-resolution realtime). Like the audio codec
# this is only the codec and stays private to the core lib; video frames ride the
# same I2P datagram transport, fragmented to fit.
find_library(VPX_LIBRARY NAMES vpx REQUIRED)
find_path(VPX_INCLUDE_DIR NAMES vpx/vpx_codec.h REQUIRED)

# I2P key material + transport now come from common's bazarish::i2p (the embedded
# libi2pd), so the previously-vendored trimmed libi2pd "keys" lib and the i2pd
# submodule are gone. OpenSSL is still needed for httplib's TLS client below.
find_package(OpenSSL 3.0 REQUIRED)

add_library(bazarish_client_lib STATIC
    src/ApiClient.cpp
    src/AudioCodec.cpp
    src/AudioIo.cpp
    src/Bot.cpp
    src/Client.cpp
    src/BlobTransport.cpp
    src/CallMedia.cpp
    src/VideoCodec.cpp
    src/VideoIo.cpp
    src/FederationFetch.cpp
    src/I2pKeys.cpp
    src/I2pRouter.cpp
    src/Invite.cpp
    src/LargeBlob.cpp
    src/ProfileManager.cpp
    src/Qr.cpp
    src/Session.cpp
)
target_include_directories(bazarish_client_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_include_directories(bazarish_client_lib PRIVATE ${QRENCODE_INCLUDE_DIR} ${OPUS_INCLUDE_DIR} ${VPX_INCLUDE_DIR})
target_link_libraries(bazarish_client_lib PUBLIC Bazarish::Common Threads::Threads)
target_link_libraries(bazarish_client_lib PRIVATE ${QRENCODE_LIBRARY} ${OPUS_LIBRARY} ${VPX_LIBRARY})
target_compile_options(bazarish_client_lib PRIVATE -Wall -Wextra -Werror)
# HTTPS facades: enable httplib's TLS client. Defined PUBLIC and OpenSSL::SSL
# linked PUBLIC so every client-side target that includes httplib (CLI, bot,
# app, tests) sees the same definition (no header-only ODR mismatch). The server
# repositories are separate and unaffected.
find_package(OpenSSL 3.0 REQUIRED)
target_compile_definitions(bazarish_client_lib PUBLIC CPPHTTPLIB_OPENSSL_SUPPORT)
target_link_libraries(bazarish_client_lib PUBLIC OpenSSL::SSL)

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
find_package(Qt6 COMPONENTS Quick QuickControls2 Qml Gui Sql Multimedia QUIET)
if(Qt6_FOUND)
    qt_standard_project_setup(REQUIRES 6.5)

    # Direct SQLite calls (serialize/deserialize) seal the transcript at rest;
    # this is the same libsqlite3 Qt's QSQLITE driver links.
    find_library(SQLITE3_LIBRARY NAMES sqlite3 REQUIRED)
    find_path(SQLITE3_INCLUDE_DIR NAMES sqlite3.h REQUIRED)

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
        app/QtVideoIo.cpp
        app/QtVideoIo.hpp
        app/Identicon.cpp
        app/Identicon.hpp
        app/AvatarStore.cpp
        app/AvatarStore.hpp
    )
    target_include_directories(bazarish-app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(bazarish-app PRIVATE
        bazarish_client_lib ${QRENCODE_LIBRARY} ${SQLITE3_LIBRARY}
        Qt6::Quick Qt6::QuickControls2 Qt6::Qml Qt6::Gui Qt6::Sql Qt6::Multimedia)
    target_include_directories(bazarish-app PRIVATE ${QRENCODE_INCLUDE_DIR} ${SQLITE3_INCLUDE_DIR})
    target_compile_options(bazarish-app PRIVATE -Wall -Wextra)

    # Bundle the brand font (Roboto Mono, static Latin cuts) so the terminal/neon
    # look renders identically regardless of what is installed; non-Latin text
    # falls back to the system monospace.
    qt_add_resources(bazarish-app appfonts
        PREFIX "/fonts"
        BASE app/fonts
        FILES
            app/fonts/RobotoMono-Regular.ttf
            app/fonts/RobotoMono-Bold.ttf
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
        QML_FILES
            app/qml/Main.qml
            app/qml/Theme.qml
            app/qml/DeliveryStatus.qml
            app/qml/ProfilePicker.qml
            app/qml/CreateProfileWizard.qml
            app/qml/ConnectServerPage.qml
            app/qml/MainView.qml
            app/qml/ChatList.qml
            app/qml/AccountSwitcher.qml
            app/qml/ConversationView.qml
            app/qml/MessageBubble.qml
            app/qml/SearchPopup.qml
            app/qml/Composer.qml
            app/qml/FileSendDialog.qml
            app/qml/SaveAttachmentDialog.qml
            app/qml/NewChatSheet.qml
            app/qml/InviteSheet.qml
            app/qml/SignWithKeySheet.qml
            app/qml/ContactInfo.qml
            app/qml/GroupInfo.qml
            app/qml/CallScreen.qml
            app/qml/SettingsPage.qml
            app/qml/RouterStatusPage.qml
            app/qml/ServerHelloDialog.qml
            app/qml/components/Avatar.qml
            app/qml/components/QrView.qml
            app/qml/components/DialogButtons.qml
            app/qml/components/IconButton.qml
            app/qml/components/MenuButton.qml
            app/qml/components/FormField.qml
            app/qml/components/ServerConnectForm.qml
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
bazarish_client_add_test(TestVideoCodec)
bazarish_client_add_test(TestFacade)
bazarish_client_add_test(TestClient)
bazarish_client_add_test(TestGroup)
bazarish_client_add_test(TestI2pKeys)
bazarish_client_add_test(TestBlobTransport)
bazarish_client_add_test(TestInvite)
bazarish_client_add_test(TestLargeBlob)
bazarish_client_add_test(TestLogin)
bazarish_client_add_test(TestProfile)

# The transcript store needs Qt SQL and direct SQLite, so it has its own target.
if(Qt6_FOUND)
    add_executable(TestTranscript tests/TestTranscript.cpp app/TranscriptStore.cpp)
    target_include_directories(TestTranscript PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/app ${SQLITE3_INCLUDE_DIR})
    target_link_libraries(TestTranscript PRIVATE
        bazarish_client_lib ${SQLITE3_LIBRARY} Qt6::Core Qt6::Sql)
    target_compile_options(TestTranscript PRIVATE -Wall -Wextra)
    add_test(NAME TestTranscript COMMAND TestTranscript)
endif()
