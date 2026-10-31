# Bazarish project (c) 2026
cmake_minimum_required(VERSION 3.20)
project(bazarish-client-qt C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/common/CMakeLists.txt")
    message(FATAL_ERROR "no common library at ${CMAKE_CURRENT_SOURCE_DIR}/common"
        " - run: git submodule update --init --recursive")
endif()
add_subdirectory(common bazarish-common)

# -Wno-error: under a sanitizer GCC raises maybe-uninitialized inside libstdc++ headers.
set(BAZARISH_SANITIZE "" CACHE STRING "Build with a sanitizer: address | undefined | address,undefined")
if(BAZARISH_SANITIZE)
    add_compile_options(-fsanitize=${BAZARISH_SANITIZE} -fno-omit-frame-pointer -g -Wno-error)
    add_link_options(-fsanitize=${BAZARISH_SANITIZE})
endif()

# No Svg: the qsvg plugin draws the logo, and a missing Svg dev config would fail this find.
find_package(Qt6 COMPONENTS Quick QuickControls2 Qml Gui Multimedia Widgets Network Test QUIET)
if(Qt6_FOUND)
    qt_standard_project_setup(REQUIRES 6.5)

    # Separate: a missing DBus dev config must not fail the Qt6 find above.
    find_package(Qt6 COMPONENTS DBus QUIET)

    # Vendored QR reader (libqrencode only writes codes); not ours to lint.
    add_library(quirc STATIC
        third_party/quirc/quirc.c
        third_party/quirc/decode.c
        third_party/quirc/identify.c
        third_party/quirc/version_db.c
    )
    target_include_directories(quirc SYSTEM PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/quirc)
    target_compile_options(quirc PRIVATE -w)
    set_target_properties(quirc PROPERTIES POSITION_INDEPENDENT_CODE ON)

    # The core keeps every account and runs with no window: no Qt Quick, no Qt QML.
    add_library(bazarish_app_shared STATIC
        app/SingleInstance.cpp
        app/SingleInstance.hpp
        app/AppSettings.cpp
        app/I2pEvents.hpp
        app/AccountCommands.hpp
        app/AccountEvents.hpp
        app/AppCommands.hpp
        app/AppEvents.hpp
        app/CoreSocket.cpp
        app/CoreSocket.hpp
        app/ContactBook.cpp
        app/ContactBook.hpp
        app/Documents.cpp
        app/Documents.hpp
        app/Wire.cpp
        app/Wire.hpp
        app/SessionShared.cpp
        app/SessionShared.hpp
        app/SystemNotes.cpp
        app/SystemNotes.hpp
        app/ChatPreview.cpp
        app/ChatPreview.hpp
        app/Translations.cpp
        app/Translations.hpp
        app/TranscriptStore.cpp
        app/TranscriptStore.hpp
        app/QtAudioIo.cpp
        app/QtAudioIo.hpp
        app/AudioRoute.cpp
        app/AudioRoute.hpp
        app/VoiceNote.cpp
    )
    target_include_directories(bazarish_app_shared PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/app
        ${SQLCIPHER_INCLUDE_DIR})
    # No Qt6::Sql: its QSQLITE driver links plain libsqlite3, whose symbols SQLCipher exports too.
    target_link_libraries(bazarish_app_shared PUBLIC bazarish_client ${SQLCIPHER_LIBRARY}
        Qt6::Core Qt6::Multimedia Qt6::Network)
    target_compile_options(bazarish_app_shared PRIVATE -Wall -Wextra)

    add_library(bazarish_app_core STATIC
        app/CoreMain.cpp
        app/CoreMain.hpp
        app/I2pService.cpp
        app/I2pService.hpp
        app/CoreHost.cpp
        app/CoreHost.hpp
        app/CoreAccount.cpp
        app/CoreAccount.hpp
        app/CoreCalls.cpp
        app/CoreChats.cpp
        app/CoreContacts.cpp
        app/CoreSending.cpp
        app/CoreTransfers.cpp
        app/MessageIngest.cpp
        app/MessageIngest.hpp
        app/OperationLog.cpp
        app/OperationLog.hpp
        app/SessionWorker.cpp
        app/SessionWorker.hpp
        app/CallTones.cpp
        app/CallTones.hpp
        app/Notifier.cpp
        app/Notifier.hpp
        app/NotifySound.cpp
        app/NotifySound.hpp
        app/Ringtone.cpp
        app/Ringtone.hpp
    )
    target_link_libraries(bazarish_app_core PUBLIC bazarish_app_shared Qt6::QmlIntegration)
    if(ANDROID)
        target_link_libraries(bazarish_app_core PRIVATE Qt6::CorePrivate)
    endif()
    target_compile_options(bazarish_app_core PRIVATE -Wall -Wextra)

    add_library(bazarish_app_ui STATIC
        app/AppController.cpp
        app/SelectionHold.cpp
        app/PopupStack.cpp
        app/AppController.hpp
        app/I2pController.cpp
        app/I2pController.hpp
        app/SessionController.cpp
        app/SessionController.hpp
        app/SessionCalls.cpp
        app/SessionVoice.cpp
        app/SessionTransfers.cpp
        app/SessionReactions.cpp
        app/SessionReceive.cpp
        app/SessionContacts.cpp
        app/Markup.cpp
        app/Markup.hpp
        app/EmojiText.cpp
        app/EmojiText.hpp
        app/AppFont.cpp
        app/AppFont.hpp
        app/Picture.cpp
        app/Picture.hpp
        app/Models.cpp
        app/Models.hpp
        app/DesktopEntry.cpp
        app/DesktopEntry.hpp
        app/TrayIcon.cpp
        app/TrayIcon.hpp
        app/Identicon.cpp
        app/Identicon.hpp
        app/LetterAvatar.cpp
        app/LetterAvatar.hpp
        app/AvatarStore.cpp
        app/PictureStore.cpp
        app/AvatarStore.hpp
    )
    target_include_directories(bazarish_app_ui PRIVATE ${QRENCODE_INCLUDE_DIR})
    target_link_libraries(bazarish_app_ui PUBLIC bazarish_app_shared quirc ${QRENCODE_LIBRARY}
        Qt6::Quick Qt6::QuickControls2 Qt6::Qml Qt6::Gui Qt6::Widgets)
    target_compile_options(bazarish_app_ui PRIVATE -Wall -Wextra)
    bazarish_stamp_version(bazarish_app_ui)

    qt_add_executable(bazarish-app
        app/main.cpp
        app/DeliveryStatus.hpp
        app/PhotoShot.cpp
        app/PhotoShot.hpp
        app/QrScanner.cpp
        app/QrScanner.hpp
    )
    target_link_libraries(bazarish-app PRIVATE bazarish_app_core bazarish_app_ui)
    target_include_directories(bazarish-app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/app
        ${QRENCODE_INCLUDE_DIR} ${SQLCIPHER_INCLUDE_DIR})
    target_compile_options(bazarish-app PRIVATE -Wall -Wextra)
    if(NOT QT6_IS_SHARED_LIBS_BUILD)
        target_compile_definitions(bazarish-app PRIVATE BAZARISH_STATIC_QT)
        target_compile_definitions(bazarish_app_ui PRIVATE BAZARISH_STATIC_QT)
        if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
            qt_import_plugins(bazarish-app INCLUDE Qt6::QXdgDesktopPortalThemePlugin)
        endif()
    endif()

    if(APPLE)
        set_target_properties(bazarish-app PROPERTIES
            MACOSX_BUNDLE TRUE
            MACOSX_BUNDLE_INFO_PLIST
                ${CMAKE_CURRENT_SOURCE_DIR}/packaging/macos/Info.plist.in
            MACOSX_BUNDLE_GUI_IDENTIFIER sh.bazari.client
            MACOSX_BUNDLE_BUNDLE_NAME Bazarish
            MACOSX_BUNDLE_EXECUTABLE_NAME bazarish-app)
    endif()

    if(ANDROID)
        # 31 is Android 12, the first with Notification.CallStyle for the incoming call.
        set(kAndroidMinSdk 31)
        set(kAndroidTargetSdk 34)
        target_sources(bazarish-app PRIVATE app/AndroidShade.cpp app/AndroidShade.hpp
            app/AndroidWindow.cpp app/AndroidWindow.hpp
            app/AndroidCallAudio.cpp app/AndroidCallAudio.hpp)
        bazarish_git_version(${CMAKE_CURRENT_SOURCE_DIR} kAndroidVersionName kAndroidVersionCode)
        set_target_properties(bazarish-app PROPERTIES
            QT_ANDROID_VERSION_NAME "${kAndroidVersionName}"
            QT_ANDROID_VERSION_CODE ${kAndroidVersionCode}
            QT_ANDROID_PACKAGE_NAME sh.bazari.client
            QT_ANDROID_MIN_SDK_VERSION ${kAndroidMinSdk}
            QT_ANDROID_TARGET_SDK_VERSION ${kAndroidTargetSdk}
            QT_ANDROID_PACKAGE_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/packaging/android/source)
    endif()

    if(WIN32)
        # Without RC enabled the .rc file is silently skipped and the binary gets no icon.
        enable_language(RC)
        set_target_properties(bazarish-app PROPERTIES WIN32_EXECUTABLE TRUE)
        target_sources(bazarish-app PRIVATE app/icon/bazarish.rc)
        set_source_files_properties(app/icon/bazarish.rc PROPERTIES
            COMPILE_OPTIONS "-I${CMAKE_CURRENT_SOURCE_DIR}/app/icon")
        target_sources(bazarish-app PRIVATE app/UrlScheme.cpp app/UrlScheme.hpp)
    endif()

    if(TARGET Qt6::DBus)
        target_link_libraries(bazarish_app_ui PRIVATE Qt6::DBus)
        target_compile_definitions(bazarish_app_ui PRIVATE BAZARISH_HAVE_QTDBUS)
    endif()

    qt_add_resources(bazarish-app appfonts
        PREFIX "/fonts"
        BASE app/fonts
        FILES
            app/fonts/RobotoMono-Regular.ttf
            app/fonts/RobotoMono-Bold.ttf
            app/fonts/TwemojiMozilla.ttf
    )

    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    add_custom_target(check-translations ALL
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/check-translations.py
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Checking that every string shown has every language"
        VERBATIM)
    add_dependencies(bazarish-app check-translations)

    qt_add_resources(bazarish_app_shared appi18n
        PREFIX "/i18n"
        BASE app/i18n
        FILES
            app/i18n/messages.json
    )

    qt_add_resources(bazarish_app_core appsound
        PREFIX "/sound"
        BASE app/sound
        FILES
            app/sound/notify.wav
            app/sound/reaction.wav
            app/sound/ringtone.wav
    )

    qt_add_resources(bazarish-app appicon
        PREFIX "/icon"
        BASE app/icon
        FILES
            app/icon/bazarish.png
            app/icon/logo.svg
            app/icon/logo-onlight.svg
    )

    set_source_files_properties(app/qml/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
    set_source_files_properties(app/qml/components/TextSelection.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
    qt_add_qml_module(bazarish-app
        URI Bazarish
        VERSION 1.0
        # Explicit: the default prefix moved in Qt 6.5, and the engine loads one fixed URL.
        RESOURCE_PREFIX "/qt/qml"
        # QrScanner's C++ sink property is a QVideoSink.
        DEPENDENCIES QtMultimedia
        QML_FILES
            app/qml/Main.qml
            app/qml/Theme.qml
            app/qml/Format.js
            app/qml/Emoji.js
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
            app/qml/EmojiPicker.qml
            app/qml/ForwardSheet.qml
            app/qml/BotMenuSheet.qml
            app/qml/Composer.qml
            app/qml/FileSendDialog.qml
            app/qml/VoiceRecorder.qml
            app/qml/PhotoCapture.qml
            app/qml/NewChatSheet.qml
        app/qml/PairDeviceSheet.qml
        app/qml/PairThisDeviceSheet.qml
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
            app/qml/GatewayFirstRunDialog.qml
            app/qml/ServerHelloDialog.qml
            app/qml/AccountDeleteConfirmDialog.qml
            app/qml/AddressChoiceDialog.qml
            app/qml/AccountDeleteLockedDialog.qml
            app/qml/AccountDeleteFailedDialog.qml
            app/qml/OperationsOverlay.qml
            app/qml/OperationRow.qml
            app/qml/ConnectionStrip.qml
            app/qml/components/Avatar.qml
            app/qml/components/AvatarViewer.qml
            app/qml/components/SavedMark.qml
            app/qml/components/QrView.qml
            app/qml/components/CameraSheet.qml
            app/qml/components/QrScanSheet.qml
            app/qml/components/QrScanButton.qml
            app/qml/components/AvatarCropDialog.qml
            app/qml/components/Toggle.qml
            app/qml/components/ContextMenu.qml
            app/qml/components/FormatChip.qml
            app/qml/components/SelectionTools.qml
            app/qml/components/TextSelection.qml
            app/qml/components/TextField.qml
            app/qml/components/TextArea.qml
            app/qml/components/PhoneSelection.qml
            app/qml/components/FocusSelection.qml
            app/qml/components/ContextMenuItem.qml
            app/qml/components/CameraGate.qml
            app/qml/components/Dropdown.qml
            app/qml/components/UnreadBadge.qml
            app/qml/components/ButtonRow.qml
            app/qml/components/ScrollColumn.qml
            app/qml/components/ScrollBoost.qml
            app/qml/components/DialogButtons.qml
            app/qml/components/DialogFrame.qml
            app/qml/components/Hairline.qml
            app/qml/components/Label.qml
            app/qml/components/Icon.qml
            app/qml/components/IconButton.qml
            app/qml/components/IconLabel.qml
            app/qml/components/MenuButton.qml
            app/qml/components/MicrophoneGate.qml
            app/qml/components/FormField.qml
            app/qml/components/ModeTab.qml
            app/qml/components/IconTab.qml
            app/qml/components/ShareLinkSheet.qml
            app/qml/components/ServerConnectForm.qml
            app/qml/components/ConnectProgressDialog.qml
            app/qml/components/LinkWarningDialog.qml
            app/qml/components/CallGlow.qml
            app/qml/components/CallButton.qml
            app/qml/components/CopyableError.qml
    )
    message(STATUS "Qt6 Quick found: building bazarish-app")
else()
    message(STATUS "Qt6 Quick not found: skipping bazarish-app")
endif()

enable_testing()

add_executable(TestAppSettings tests/TestAppSettings.cpp app/AppSettings.cpp)
target_include_directories(TestAppSettings PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/app)
target_link_libraries(TestAppSettings PRIVATE bazarish_client)
target_compile_options(TestAppSettings PRIVATE -Wall -Wextra)
add_test(NAME TestAppSettings COMMAND TestAppSettings)

if(Qt6_FOUND)
    add_executable(TestMarkup tests/TestMarkup.cpp app/Markup.cpp)
    target_include_directories(TestMarkup PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/app)
    target_link_libraries(TestMarkup PRIVATE bazarish_client Qt6::Core)
    target_compile_options(TestMarkup PRIVATE -Wall -Wextra -Werror)
    add_test(NAME TestMarkup COMMAND TestMarkup)

    add_executable(TestQrScan tests/TestQrScan.cpp app/QrScanner.cpp)
    target_include_directories(TestQrScan PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/app ${QRENCODE_INCLUDE_DIR})
    target_link_libraries(TestQrScan PRIVATE
        bazarish_client quirc ${QRENCODE_LIBRARY} Qt6::Gui Qt6::Multimedia Qt6::Qml)
    target_compile_options(TestQrScan PRIVATE -Wall -Wextra -Werror)
    add_test(NAME TestQrScan COMMAND TestQrScan)

    # Android's back key closes the top popup first; which one that is rests on Qt internals.
    add_executable(TestPopupStack tests/TestPopupStack.cpp app/PopupStack.cpp)
    target_include_directories(TestPopupStack PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/app)
    target_link_libraries(TestPopupStack PRIVATE Qt6::Quick Qt6::Qml)
    target_compile_options(TestPopupStack PRIVATE -Wall -Wextra -Werror)
    add_test(NAME TestPopupStack COMMAND TestPopupStack)
    set_tests_properties(TestPopupStack PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_CONTROLS_STYLE=Basic")

    if(TARGET Qt6::Test)
        add_executable(TestSelectionHold tests/TestSelectionHold.cpp app/SelectionHold.cpp)
        target_include_directories(TestSelectionHold PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/app)
        target_link_libraries(TestSelectionHold PRIVATE Qt6::Quick Qt6::Test)
        target_compile_options(TestSelectionHold PRIVATE -Wall -Wextra -Werror)
        add_test(NAME TestSelectionHold COMMAND TestSelectionHold)
        set_tests_properties(TestSelectionHold PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    endif()

    add_executable(TestPictureStore tests/TestPictureStore.cpp app/PictureStore.cpp)
    target_include_directories(TestPictureStore PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/app)
    target_link_libraries(TestPictureStore PRIVATE Qt6::Quick)
    target_compile_options(TestPictureStore PRIVATE -Wall -Wextra -Werror)
    add_test(NAME TestPictureStore COMMAND TestPictureStore)
    set_tests_properties(TestPictureStore PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    add_executable(TestPicture tests/TestPicture.cpp app/Picture.cpp)
    target_include_directories(TestPicture PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/app)
    target_link_libraries(TestPicture PRIVATE Qt6::Gui)
    target_compile_options(TestPicture PRIVATE -Wall -Wextra -Werror)
    add_test(NAME TestPicture COMMAND TestPicture)
    set_tests_properties(TestPicture PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    add_executable(TestEmojiText tests/TestEmojiText.cpp app/EmojiText.cpp)
    target_include_directories(TestEmojiText PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/app)
    target_link_libraries(TestEmojiText PRIVATE Qt6::Core)
    target_compile_options(TestEmojiText PRIVATE -Wall -Wextra -Werror)
    add_test(NAME TestEmojiText COMMAND TestEmojiText)

    add_executable(TestEmojiSet tests/TestEmojiSet.cpp app/AppFont.cpp app/EmojiText.cpp)
    qt_add_resources(TestEmojiSet testfonts
        PREFIX "/fonts"
        BASE app/fonts
        FILES
            app/fonts/RobotoMono-Regular.ttf
            app/fonts/RobotoMono-Bold.ttf
            app/fonts/TwemojiMozilla.ttf
    )
    target_include_directories(TestEmojiSet PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/app)
    target_compile_definitions(TestEmojiSet PRIVATE
        BAZARISH_EMOJI_JS="${CMAKE_CURRENT_SOURCE_DIR}/app/qml/Emoji.js")
    target_link_libraries(TestEmojiSet PRIVATE bazarish_common Qt6::Gui Qt6::Qml)
    target_compile_options(TestEmojiSet PRIVATE -Wall -Wextra -Werror)
    add_test(NAME TestEmojiSet COMMAND TestEmojiSet)
    # Offscreen draws with CoreText on macOS; the app draws with FreeType there, as everywhere.
    if(APPLE)
        set_tests_properties(TestEmojiSet PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=cocoa:fontengine=freetype")
    else()
        set_tests_properties(TestEmojiSet PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    endif()

    add_executable(TestConversationModel tests/TestConversationModel.cpp app/Models.cpp
        app/Models.hpp)
    target_link_libraries(TestConversationModel PRIVATE bazarish_app_shared Qt6::Gui Qt6::Qml)
    target_compile_options(TestConversationModel PRIVATE -Wall -Wextra -Werror)
    add_test(NAME TestConversationModel COMMAND TestConversationModel)
    set_tests_properties(TestConversationModel PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    add_executable(TestLetterAvatar tests/TestLetterAvatar.cpp app/LetterAvatar.cpp)
    target_include_directories(TestLetterAvatar PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/app)
    target_link_libraries(TestLetterAvatar PRIVATE Qt6::Gui)
    target_compile_options(TestLetterAvatar PRIVATE -Wall -Wextra -Werror)
    add_test(NAME TestLetterAvatar COMMAND TestLetterAvatar)

    add_executable(TestTranscript tests/TestTranscript.cpp app/TranscriptStore.cpp
        app/SystemNotes.cpp)
    target_include_directories(TestTranscript PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/app ${SQLCIPHER_INCLUDE_DIR})
    target_link_libraries(TestTranscript PRIVATE
        bazarish_client ${SQLCIPHER_LIBRARY} Qt6::Core)
    target_compile_options(TestTranscript PRIVATE -Wall -Wextra)
    add_test(NAME TestTranscript COMMAND TestTranscript)

    add_executable(TestDownloads tests/TestDownloads.cpp app/Documents.cpp)
    target_include_directories(TestDownloads PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/app)
    target_link_libraries(TestDownloads PRIVATE bazarish_client Qt6::Core)
    target_compile_options(TestDownloads PRIVATE -Wall -Wextra -Werror)
    add_test(NAME TestDownloads COMMAND TestDownloads)

    add_executable(TestIngest tests/TestIngest.cpp app/MessageIngest.cpp app/ContactBook.cpp
        app/TranscriptStore.cpp app/SystemNotes.cpp app/SessionShared.cpp)
    target_include_directories(TestIngest PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/app ${SQLCIPHER_INCLUDE_DIR})
    target_link_libraries(TestIngest PRIVATE
        bazarish_client ${SQLCIPHER_LIBRARY} Qt6::Core Qt6::QmlIntegration)
    target_compile_options(TestIngest PRIVATE -Wall -Wextra -Werror)
    add_test(NAME TestIngest COMMAND TestIngest)

    add_executable(TestWire tests/TestWire.cpp app/Wire.cpp app/AccountCommands.hpp
        app/AccountEvents.hpp app/AppCommands.hpp app/AppEvents.hpp app/I2pEvents.hpp)
    target_include_directories(TestWire PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/app)
    target_link_libraries(TestWire PRIVATE bazarish_client Qt6::Core)
    target_compile_options(TestWire PRIVATE -Wall -Wextra -Werror)
    add_test(NAME TestWire COMMAND TestWire)

    add_executable(TestSocketChannel tests/TestSocketChannel.cpp app/CoreSocket.cpp app/Wire.cpp
        app/AppSettings.cpp)
    target_include_directories(TestSocketChannel PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/app)
    target_link_libraries(TestSocketChannel PRIVATE bazarish_client Qt6::Core Qt6::Network)
    target_compile_options(TestSocketChannel PRIVATE -Wall -Wextra -Werror)
    add_test(NAME TestSocketChannel COMMAND TestSocketChannel)

    # The core alone, as Android runs it: linked with nothing of the window.
    add_executable(bazarish-core-harness tests/CoreHarness.cpp)
    target_link_libraries(bazarish-core-harness PRIVATE bazarish_app_core)

    add_executable(TestTwoProcess tests/TestTwoProcess.cpp app/CoreSocket.cpp app/Wire.cpp
        app/AppSettings.cpp app/AppCommands.hpp app/AppEvents.hpp app/AccountEvents.hpp
        app/I2pEvents.hpp)
    target_include_directories(TestTwoProcess PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/app)
    target_link_libraries(TestTwoProcess PRIVATE bazarish_client Qt6::Core Qt6::Network)
    target_compile_options(TestTwoProcess PRIVATE -Wall -Wextra -Werror)
    add_test(NAME TestTwoProcess COMMAND TestTwoProcess $<TARGET_FILE:bazarish-core-harness>)
endif()
