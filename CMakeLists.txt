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

add_library(bazarish_client_lib STATIC
    src/ApiClient.cpp
    src/Client.cpp
    src/Invite.cpp
    src/ProfileManager.cpp
    src/Qr.cpp
    src/Session.cpp
)
target_include_directories(bazarish_client_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_include_directories(bazarish_client_lib PRIVATE ${QRENCODE_INCLUDE_DIR})
target_link_libraries(bazarish_client_lib PUBLIC Bazarish::Common Threads::Threads)
target_link_libraries(bazarish_client_lib PRIVATE ${QRENCODE_LIBRARY})
target_compile_options(bazarish_client_lib PRIVATE -Wall -Wextra -Werror)

add_executable(bazarish-client src/Main.cpp)
target_link_libraries(bazarish-client PRIVATE bazarish_client_lib)
target_compile_options(bazarish-client PRIVATE -Wall -Wextra -Werror)

# The Qt6 Quick (QML) desktop app — the primary front-end over the same
# Session logic the CLI uses. Built only when Qt6 Quick is available.
find_package(Qt6 COMPONENTS Quick QuickControls2 Qml Gui Sql QUIET)
if(Qt6_FOUND)
    qt_standard_project_setup(REQUIRES 6.5)

    qt_add_executable(bazarish-app
        app/main.cpp
        app/AppController.cpp
        app/AppController.hpp
        app/SessionController.cpp
        app/SessionController.hpp
        app/Models.cpp
        app/Models.hpp
        app/TranscriptStore.cpp
        app/TranscriptStore.hpp
        app/Identicon.cpp
        app/Identicon.hpp
    )
    target_include_directories(bazarish-app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(bazarish-app PRIVATE
        bazarish_client_lib ${QRENCODE_LIBRARY}
        Qt6::Quick Qt6::QuickControls2 Qt6::Qml Qt6::Gui Qt6::Sql)
    target_include_directories(bazarish-app PRIVATE ${QRENCODE_INCLUDE_DIR})
    target_compile_options(bazarish-app PRIVATE -Wall -Wextra)

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
            app/qml/ConversationView.qml
            app/qml/MessageBubble.qml
            app/qml/Composer.qml
            app/qml/NewChatSheet.qml
            app/qml/InviteSheet.qml
            app/qml/ContactInfo.qml
            app/qml/CallScreen.qml
            app/qml/SettingsPage.qml
            app/qml/components/Avatar.qml
            app/qml/components/QrView.qml
            app/qml/components/IconButton.qml
            app/qml/components/FormField.qml
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
bazarish_client_add_test(TestClient)
bazarish_client_add_test(TestInvite)
bazarish_client_add_test(TestProfile)
