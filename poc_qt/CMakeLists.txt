cmake_minimum_required(VERSION 3.20)
project(ZipFX_Qt_POC LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

include(FetchContent)

# zlib (required by libzip)
FetchContent_Declare(zlib GIT_REPOSITORY https://github.com/madler/zlib.git GIT_TAG v1.3.1 GIT_SHALLOW TRUE)
set(ZLIB_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(ZLIB_INSTALL      OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(zlib)

# libzip
set(ENABLE_COMMONCRYPTO OFF CACHE BOOL "" FORCE)
set(ENABLE_GNUTLS      OFF CACHE BOOL "" FORCE)
set(ENABLE_MBEDTLS     OFF CACHE BOOL "" FORCE)
set(ENABLE_OPENSSL     OFF CACHE BOOL "" FORCE)
set(BUILD_TOOLS        OFF CACHE BOOL "" FORCE)
set(BUILD_REGRESS      OFF CACHE BOOL "" FORCE)
set(BUILD_EXAMPLES     OFF CACHE BOOL "" FORCE)
set(BUILD_DOC          OFF CACHE BOOL "" FORCE)
FetchContent_Declare(libzip GIT_REPOSITORY https://github.com/nih-at/libzip.git GIT_TAG v1.11.3 GIT_SHALLOW TRUE)

# Use a custom FindZLIB.cmake that resolves zlibstatic target
# (avoids libzip searching for a library file that doesn't exist yet)
list(INSERT CMAKE_MODULE_PATH 0 "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
set(HAVE_SNPRINTF TRUE CACHE INTERNAL "")

FetchContent_MakeAvailable(libzip)

# Qt6 — system-installed (not FetchContent, too large)
find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets)

add_executable(qt_poc main.cpp)
target_link_libraries(qt_poc PRIVATE
    zip zlibstatic
    Qt${QT_VERSION_MAJOR}::Widgets)
target_include_directories(qt_poc PRIVATE
    ${libzip_SOURCE_DIR}/lib ${libzip_BINARY_DIR}/lib)
