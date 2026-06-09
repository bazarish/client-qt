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

add_library(bazarish_client_lib STATIC
    src/ApiClient.cpp
    src/Client.cpp
    src/Session.cpp
)
target_include_directories(bazarish_client_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(bazarish_client_lib PUBLIC Bazarish::Common Threads::Threads)
target_compile_options(bazarish_client_lib PRIVATE -Wall -Wextra -Werror)

add_executable(bazarish-client src/Main.cpp)
target_link_libraries(bazarish-client PRIVATE bazarish_client_lib)
target_compile_options(bazarish-client PRIVATE -Wall -Wextra -Werror)

# The Qt6 desktop GUI, built only when Qt6 Widgets is available. It is a thin
# front-end over the same Session logic the CLI uses.
find_package(Qt6 COMPONENTS Widgets QUIET)
if(Qt6_FOUND)
    add_executable(bazarish-gui gui/GuiMain.cpp gui/MainWindow.cpp gui/MainWindow.hpp)
    set_target_properties(bazarish-gui PROPERTIES AUTOMOC ON)
    target_include_directories(bazarish-gui PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(bazarish-gui PRIVATE bazarish_client_lib Qt6::Widgets)
    target_compile_options(bazarish-gui PRIVATE -Wall -Wextra)
    message(STATUS "Qt6 found: building bazarish-gui")
else()
    message(STATUS "Qt6 not found: skipping bazarish-gui")
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
