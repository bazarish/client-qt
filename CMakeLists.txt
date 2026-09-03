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

# The client core lives in common: it has two front-ends, this application and
# the daemon, and neither owns it. What stays here is the user interface.
find_package(OpenSSL 3.5 REQUIRED)

# Sanitizer build (address / undefined), for chasing a crash rather than for
# shipping: BAZARISH_SANITIZE=address. It also drops -Werror, because under a
# sanitizer GCC raises maybe-uninitialized inside libstdc++ headers.
set(BAZARISH_SANITIZE "" CACHE STRING "Build with a sanitizer: address | undefined | address,undefined")
if(BAZARISH_SANITIZE)
    add_compile_options(-fsanitize=${BAZARISH_SANITIZE} -fno-omit-frame-pointer -g -Wno-error)
    add_link_options(-fsanitize=${BAZARISH_SANITIZE})
endif()

# The Qt6 Quick (QML) desktop app - what this repository builds. Built only when
# Qt6 Quick is available.
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
        app/AppSettings.cpp
        app/AppController.hpp
        app/I2pController.cpp
        app/I2pController.hpp
        app/SessionController.cpp
        app/SessionController.hpp
        app/Markup.cpp
        app/Markup.hpp
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
        app/Ringtone.cpp
        app/Ringtone.hpp
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
        bazarish_client ${QRENCODE_LIBRARY} ${SQLCIPHER_LIBRARY}
        Qt6::Quick Qt6::QuickControls2 Qt6::Qml Qt6::Gui Qt6::Multimedia Qt6::Widgets
        Qt6::Network)
    target_include_directories(bazarish-app PRIVATE ${QRENCODE_INCLUDE_DIR} ${SQLITE3_INCLUDE_DIR})
    target_compile_options(bazarish-app PRIVATE -Wall -Wextra)

    if(WIN32)
        # A GUI binary: started from the shell it opens no console window, and
        # --console is what attaches one when the log has to be read. The icon
        # the shell draws is the resource compiled in here.
        set_target_properties(bazarish-app PROPERTIES WIN32_EXECUTABLE TRUE)
        target_sources(bazarish-app PRIVATE app/icon/bazarish.rc)
        set_source_files_properties(app/icon/bazarish.rc PROPERTIES
            COMPILE_OPTIONS "-I${CMAKE_CURRENT_SOURCE_DIR}/app/icon")
    endif()

    # Reveal-in-file-manager (FileManager1 over D-Bus) when the optional module is
    # present; otherwise the code path is compiled out and falls back gracefully.
    if(TARGET Qt6::DBus)
        target_link_libraries(bazarish-app PRIVATE Qt6::DBus)
        target_compile_definitions(bazarish-app PRIVATE BAZARISH_HAVE_QTDBUS)
    endif()

    # Bundle the brand font (Roboto Mono, Latin, Greek and Cyrillic) so the
    # terminal/neon look renders identically regardless of what is installed.
    # A script the font does not cover falls back to the system monospace, and
    # what that is differs enough between desktops to be visible.
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
            app/sound/ringtone.wav
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
            app/qml/IncomingCallWindow.qml
            app/qml/AppSettingsPage.qml
            app/qml/SettingsPage.qml
            app/qml/RouterStatusPage.qml
            app/qml/ConnectionLogPage.qml
        app/qml/StoragePage.qml
            app/qml/ProxyRestartDialog.qml
            app/qml/SamRestartDialog.qml
            app/qml/ServerHelloDialog.qml
            app/qml/AccountDeleteConfirmDialog.qml
            app/qml/AddressChoiceDialog.qml
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
            app/qml/components/LinkWarningDialog.qml
            app/qml/components/CallGlow.qml
    )
    message(STATUS "Qt6 Quick found: building bazarish-app")
else()
    message(STATUS "Qt6 Quick not found: skipping bazarish-app")
endif()

enable_testing()

# The core's own tests moved into common with it; what is tested here is the
# application. The settings document is plain JSON on disk, so it needs no Qt.
add_executable(TestAppSettings tests/TestAppSettings.cpp app/AppSettings.cpp)
target_include_directories(TestAppSettings PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/app)
target_link_libraries(TestAppSettings PRIVATE bazarish_client)
target_compile_options(TestAppSettings PRIVATE -Wall -Wextra)
add_test(NAME TestAppSettings COMMAND TestAppSettings)

# The transcript store needs Qt SQL and direct SQLite, so it has its own target.
if(Qt6_FOUND)
    # Message formatting: what a body is split into and the rich text built from
    # it. Strings only - no window, no session.
    add_executable(TestMarkup tests/TestMarkup.cpp app/Markup.cpp)
    target_include_directories(TestMarkup PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/app)
    target_link_libraries(TestMarkup PRIVATE bazarish_client Qt6::Core)
    target_compile_options(TestMarkup PRIVATE -Wall -Wextra -Werror)
    add_test(NAME TestMarkup COMMAND TestMarkup)

    add_executable(TestTranscript tests/TestTranscript.cpp app/TranscriptStore.cpp)
    target_include_directories(TestTranscript PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/app ${SQLITE3_INCLUDE_DIR})
    target_link_libraries(TestTranscript PRIVATE
        bazarish_client ${SQLCIPHER_LIBRARY} Qt6::Core)
    target_compile_options(TestTranscript PRIVATE -Wall -Wextra)
    add_test(NAME TestTranscript COMMAND TestTranscript)
endif()
