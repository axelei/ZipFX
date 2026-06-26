cmake_minimum_required(VERSION 3.20)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
project(ZipFX VERSION 0.2.0 LANGUAGES C CXX)
if(APPLE)
    enable_language(OBJCXX)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_AUTOMOC ON)

# Dependencies build as shared libraries (libzip.dll, libadf.dll) on Windows.
# Static linking would require libzip's export rules to handle zlibstatic — they don't.

# Force the correct MinGW make program (CLion sometimes picks Gow's make.exe)
if(WIN32 AND EXISTS "C:/Qt/Tools/mingw1310_64/bin/mingw32-make.exe")
    set(CMAKE_MAKE_PROGRAM "C:/Qt/Tools/mingw1310_64/bin/mingw32-make.exe" CACHE FILEPATH "MinGW make" FORCE)
endif()

option(BUILD_TESTING "Build unit tests" ON)

include(FetchContent)

# ====================================================================
# zlib  –  required by libzip (and TarGzEngine)
# ====================================================================
FetchContent_Declare(
    zlib
    GIT_REPOSITORY https://github.com/madler/zlib.git
    GIT_TAG        v1.3.1
    GIT_SHALLOW    TRUE
)
set(ZLIB_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(ZLIB_INSTALL        OFF CACHE BOOL "" FORCE)
set(SKIP_INSTALL_ALL    ON  CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS   OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(zlib)
set(BUILD_SHARED_LIBS   ON  CACHE BOOL "" FORCE)
set(SKIP_INSTALL_ALL    OFF CACHE BOOL "" FORCE)

# Custom Find modules that alias FetchContent targets for downstream deps
list(INSERT CMAKE_MODULE_PATH 0 "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
set(HAVE_SNPRINTF TRUE CACHE INTERNAL "")

# ====================================================================
# bzip2  –  bzip2 compression (for libarchive tar.bz2 support)
# ====================================================================
FetchContent_Declare(
    bzip2
    GIT_REPOSITORY https://github.com/libarchive/bzip2.git
    GIT_TAG        bzip2-1.0.8
    GIT_SHALLOW    TRUE
    DOWNLOAD_ONLY  TRUE
)
FetchContent_MakeAvailable(bzip2)
add_library(bz2_static STATIC
    ${bzip2_SOURCE_DIR}/blocksort.c
    ${bzip2_SOURCE_DIR}/huffman.c
    ${bzip2_SOURCE_DIR}/crctable.c
    ${bzip2_SOURCE_DIR}/randtable.c
    ${bzip2_SOURCE_DIR}/compress.c
    ${bzip2_SOURCE_DIR}/decompress.c
    ${bzip2_SOURCE_DIR}/bzlib.c
)
target_include_directories(bz2_static PUBLIC ${bzip2_SOURCE_DIR})

# ====================================================================
# xz (liblzma)  –  xz/lzma compression (for libarchive tar.xz support)
# ====================================================================
set(BUILD_TESTING        OFF CACHE BOOL "" FORCE)
set(CREATE_XZ_SYMLINKS   OFF CACHE BOOL "" FORCE)
set(CREATE_LZMA_SYMLINKS OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS    OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    liblzma
    GIT_REPOSITORY https://github.com/tukaani-project/xz.git
    GIT_TAG        v5.6.4
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(liblzma)
set(BUILD_SHARED_LIBS    ON  CACHE BOOL "" FORCE)
set(BUILD_TESTING ON CACHE BOOL "" FORCE)

# ====================================================================
# zstd  –  Zstandard compression (for libarchive tar.zst support)
# ====================================================================
set(ZSTD_BUILD_PROGRAMS  OFF CACHE BOOL "" FORCE)
set(ZSTD_BUILD_TESTS     OFF CACHE BOOL "" FORCE)
set(ZSTD_BUILD_SHARED    OFF CACHE BOOL "" FORCE)
set(ZSTD_BUILD_STATIC    ON  CACHE BOOL "" FORCE)
FetchContent_Declare(
    zstd
    GIT_REPOSITORY https://github.com/facebook/zstd.git
    GIT_TAG        v1.5.7
    GIT_SHALLOW    TRUE
    SOURCE_SUBDIR  build/cmake
)
FetchContent_MakeAvailable(zstd)

# ====================================================================
# lz4  –  LZ4 compression (for libarchive tar.lz4 support)
# ====================================================================
set(LZ4_BUILD_CLI         OFF CACHE BOOL "" FORCE)
set(LZ4_BUILD_LEGACY_LZ4C OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS     OFF CACHE BOOL "" FORCE)
set(BUILD_STATIC_LIBS     ON  CACHE BOOL "" FORCE)
FetchContent_Declare(
    lz4
    GIT_REPOSITORY https://github.com/lz4/lz4.git
    GIT_TAG        v1.10.0
    GIT_SHALLOW    TRUE
    SOURCE_SUBDIR  build/cmake
)
FetchContent_MakeAvailable(lz4)

# ====================================================================
# libzip  –  ZIP read/write (in-place delete/add)
# ====================================================================
set(BUILD_SHARED_LIBS  ON  CACHE BOOL "" FORCE)
set(ENABLE_COMMONCRYPTO OFF CACHE BOOL "" FORCE)
set(ENABLE_GNUTLS      OFF CACHE BOOL "" FORCE)
set(ENABLE_MBEDTLS     OFF CACHE BOOL "" FORCE)
set(ENABLE_OPENSSL     OFF CACHE BOOL "" FORCE)
set(ENABLE_BZIP2       OFF CACHE BOOL "" FORCE)
set(ENABLE_LZMA        OFF CACHE BOOL "" FORCE)
set(BUILD_TOOLS        OFF CACHE BOOL "" FORCE)
set(BUILD_REGRESS      OFF CACHE BOOL "" FORCE)
set(BUILD_EXAMPLES     OFF CACHE BOOL "" FORCE)
set(BUILD_DOC          OFF CACHE BOOL "" FORCE)
set(LIBZIP_DO_INSTALL  OFF CACHE BOOL "" FORCE)
set(SKIP_INSTALL_ALL   ON  CACHE BOOL "" FORCE)
FetchContent_Declare(
    libzip
    GIT_REPOSITORY https://github.com/nih-at/libzip.git
    GIT_TAG        v1.11.3
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(libzip)

# ====================================================================
# miniz  –  ZIP alternative (deprecated, kept for reference)
# ====================================================================

# Pre-set cache variables so libarchive finds our FetchContent-built deps.
# BZip2 and LibLZMA use find_package(); LZ4 and Zstd use FIND_PATH/FIND_LIBRARY.
set(BZIP2_FOUND       TRUE            CACHE BOOL "" FORCE)
set(BZIP2_INCLUDE_DIR "${bzip2_SOURCE_DIR}" CACHE PATH "" FORCE)
set(BZIP2_LIBRARIES   "bz2_static"    CACHE STRING "" FORCE)

set(LIBLZMA_FOUND            TRUE CACHE BOOL "" FORCE)
set(LIBLZMA_HAS_AUTO_DECODER TRUE CACHE BOOL "" FORCE)
set(LIBLZMA_HAS_EASY_ENCODER TRUE CACHE BOOL "" FORCE)
set(LIBLZMA_HAS_LZMA_PRESET TRUE CACHE BOOL "" FORCE)
set(LIBLZMA_INCLUDE_DIR  "${liblzma_SOURCE_DIR}/src/liblzma/api" CACHE PATH "" FORCE)
set(LIBLZMA_INCLUDE_DIRS "${liblzma_SOURCE_DIR}/src/liblzma/api" CACHE PATH "" FORCE)
set(LIBLZMA_LIBRARIES    "liblzma"     CACHE STRING "" FORCE)
set(LIBLZMA_LIBRARY      "liblzma"     CACHE STRING "" FORCE)

set(LZ4_INCLUDE_DIR  "${lz4_SOURCE_DIR}/lib"  CACHE PATH "" FORCE)
set(LZ4_LIBRARY      "lz4_static"             CACHE STRING "" FORCE)

set(ZSTD_INCLUDE_DIR "${zstd_SOURCE_DIR}/lib"  CACHE PATH "" FORCE)
set(ZSTD_LIBRARY     "libzstd_static"          CACHE STRING "" FORCE)

# ====================================================================
# libarchive  –  7z read/write + RAR read + compressed tar support
# ====================================================================
set(ENABLE_ZLIB         ON  CACHE BOOL "" FORCE)
set(ENABLE_BZip2        ON  CACHE BOOL "" FORCE)
set(ENABLE_LZMA         ON  CACHE BOOL "" FORCE)
set(ENABLE_LZ4          ON  CACHE BOOL "" FORCE)
set(ENABLE_ZSTD         ON  CACHE BOOL "" FORCE)
set(ENABLE_LIBB2        OFF CACHE BOOL "" FORCE)
set(ENABLE_OPENSSL      OFF CACHE BOOL "" FORCE)
set(ENABLE_EXPAT        OFF CACHE BOOL "" FORCE)
set(ENABLE_PCREPOSIX    OFF CACHE BOOL "" FORCE)
set(ENABLE_ICONV        OFF CACHE BOOL "" FORCE)
set(ENABLE_NETTLE       OFF CACHE BOOL "" FORCE)
set(ENABLE_CNG          OFF CACHE BOOL "" FORCE)
set(ENABLE_SHARED       OFF CACHE BOOL "" FORCE)
set(ENABLE_TAR          OFF CACHE BOOL "" FORCE)
set(ENABLE_CPIO         OFF CACHE BOOL "" FORCE)
set(ENABLE_CAT          OFF CACHE BOOL "" FORCE)
set(ENABLE_UNSHAR       OFF CACHE BOOL "" FORCE)
set(ENABLE_TEST         OFF CACHE BOOL "" FORCE)
set(ENABLE_COVERAGE     OFF CACHE BOOL "" FORCE)
set(ENABLE_SANITIZERS   OFF CACHE BOOL "" FORCE)
set(ENABLE_WERROR       OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    libarchive
    GIT_REPOSITORY https://github.com/libarchive/libarchive.git
    GIT_TAG        v3.7.7
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(libarchive)

# CHECK_FUNCTION_EXISTS can't link against not-yet-built FetchContent libs,
# so patch config.h for the defines it missed.
set(_la_config "${libarchive_BINARY_DIR}/config.h")
file(READ "${_la_config}" _la_config_h)
string(REPLACE "/* #undef HAVE_LIBLZMA */"          "#define HAVE_LIBLZMA 1"          _la_config_h "${_la_config_h}")
string(REPLACE "/* #undef HAVE_LZMA_H */"           "#define HAVE_LZMA_H 1"           _la_config_h "${_la_config_h}")
string(REPLACE "/* #undef HAVE_LIBZSTD */"           "#define HAVE_LIBZSTD 1"          _la_config_h "${_la_config_h}")
string(REPLACE "/* #undef HAVE_ZSTD_compressStream */" "#define HAVE_ZSTD_compressStream 1" _la_config_h "${_la_config_h}")
string(REPLACE "/* #undef HAVE_LZMA_STREAM_ENCODER_MT */" "#define HAVE_LZMA_STREAM_ENCODER_MT 1" _la_config_h "${_la_config_h}")
file(WRITE "${_la_config}" "${_la_config_h}")

target_compile_definitions(archive_static PRIVATE LIBARCHIVE_STATIC)

# ====================================================================
# adflib  –  Amiga Disk File format (.adf)
# ====================================================================
set(ADFLIB_WARNINGS OFF CACHE BOOL "" FORCE)
set(ADFLIB_ENABLE_TESTS OFF CACHE BOOL "" FORCE)
set(ADFLIB_EXAMPLES OFF CACHE BOOL "" FORCE)
set(ADFLIB_DEV_NATIVE OFF CACHE BOOL "" FORCE)
if(NOT WIN32)
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
endif()
FetchContent_Declare(
    adflib
    GIT_REPOSITORY https://github.com/adflib/ADFlib.git
    GIT_TAG        v0.10.7
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(adflib)
if(NOT WIN32)
    set(BUILD_SHARED_LIBS ON CACHE BOOL "" FORCE)
endif()

# ====================================================================
# libchdr  –  MAME Compressed Hunks of Data (.chd) disc images
# ====================================================================
set(BUILD_SHARED_LIBS  OFF CACHE BOOL "" FORCE)
set(INSTALL_STATIC_LIBS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    libchdr
    GIT_REPOSITORY https://github.com/rtissera/libchdr.git
    GIT_TAG        v0.3.0
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(libchdr)
set(BUILD_SHARED_LIBS ON CACHE BOOL "" FORCE)

# ====================================================================
# bit7z  –  additional archive formats via 7-Zip engine (ARJ, DEB, DMG,
#            MSI, NSIS, RPM, VDI, VHD, VMDK, WIM, etc.)
# ====================================================================
set(BIT7Z_AUTO_FORMAT       ON  CACHE BOOL "" FORCE)
set(BIT7Z_USE_NATIVE_STRING OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    bit7z
    GIT_REPOSITORY https://github.com/rikyoz/bit7z.git
    GIT_TAG        v4.0.1
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(bit7z)

# bit7z uses -Werror; newer GCC flags false-positive array-bounds in std::vector
if(TARGET bit7z64 AND CMAKE_CXX_COMPILER_ID MATCHES "GNU")
    target_compile_options(bit7z64 PRIVATE -Wno-error=array-bounds)
endif()

# ====================================================================
# StormLib  –  MPQ archive support (Blizzard games: Warcraft, StarCraft, Diablo)
# ====================================================================
set(STORM_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(STORM_SKIP_INSTALL ON CACHE BOOL "" FORCE)
FetchContent_Declare(
    StormLib
    GIT_REPOSITORY https://github.com/ladislav-zezula/StormLib.git
    GIT_TAG        v9.31
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(StormLib)

# StormLib builds as 'storm' static library
if(TARGET storm)
    set_target_properties(storm PROPERTIES POSITION_INDEPENDENT_CODE ON)
endif()

# Fix missing #include <algorithm> in bit7z v4.0.1 (newer GCC no longer
# transitively includes <algorithm> from other headers)
if(TARGET bit7z64)
    foreach(_bf bitarchiveeditor.cpp bitinputarchive.cpp)
        set(_bf_path "${bit7z_SOURCE_DIR}/src/${_bf}")
        file(READ "${_bf_path}" _bf_content)
        if(NOT "${_bf_content}" MATCHES "#.*include.*<algorithm>")
            file(WRITE "${_bf_path}" "#include <algorithm>\n${_bf_content}")
        endif()
    endforeach()
endif()

# ====================================================================
# Qt6  –  cross-platform UI
# ====================================================================
# Common default installation paths per platform.
# For CI (GitHub Actions), the `jurplel/install-qt-action` sets
# CMAKE_PREFIX_PATH automatically — no changes needed.
if(WIN32)
    list(APPEND CMAKE_PREFIX_PATH "C:/Qt/6.11.1/mingw_64")
elseif(APPLE)
    list(APPEND CMAKE_PREFIX_PATH "/usr/local/opt/qt" "/opt/homebrew/opt/qt"
         "$ENV{HOME}/Qt/6.11.1/macos")
else()
    list(APPEND CMAKE_PREFIX_PATH "$ENV{HOME}/Qt/6.11.1/gcc_64")
endif()
find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets Network)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets Network)

# ====================================================================
# CLI11  –  header-only command-line parser
# ====================================================================
FetchContent_Declare(
    CLI11
    GIT_REPOSITORY https://github.com/CLIUtils/CLI11.git
    GIT_TAG        v2.4.2
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(CLI11)

# ====================================================================
# Version header  –  generated from CMake project version
# ====================================================================
configure_file(
    "${CMAKE_SOURCE_DIR}/src/version.h.in"
    "${CMAKE_BINARY_DIR}/generated/version.h"
    @ONLY
)

# ====================================================================
# Our executable
# ====================================================================
set(ZIPFX_SOURCES
    src/main.cpp
    src/ui/MainWindow.cpp
    src/ui/FileListModel.cpp
    src/ui/CreateArchiveDialog.cpp
    src/ui/FindFilesDialog.cpp
    src/engine/ArchiveEngine.cpp
    src/engine/ArchiveEngineFactory.cpp
    src/engine/ZipEngine.cpp
    src/engine/TarGzEngine.cpp
    src/engine/LibarchiveEngine.cpp
    src/engine/FileSignature.cpp
    src/engine/Bit7zEngine.cpp
    src/engine/RarEngine.cpp
    src/engine/AdfEngine.cpp
    src/engine/FlatArchiveEngine.cpp
    src/engine/WadEngine.cpp
    src/engine/PakEngine.cpp
    src/engine/HogEngine.cpp
    src/engine/GrpEngine.cpp
    src/engine/VpkEngine.cpp
    src/engine/GobEngine.cpp
    src/engine/RffEngine.cpp
    src/engine/BigEngine.cpp
    src/engine/PodEngine.cpp
    src/engine/CdiEngine.cpp
    src/engine/ChdEngine.cpp
    src/engine/MpqEngine.cpp
    src/cli/CliHandler.cpp
    src/recovery/RecoveryRecord.cpp
)

if(WIN32)
    list(APPEND ZIPFX_SOURCES src/dnd/VirtualFileDataObject.cpp)
elseif(APPLE)
    list(APPEND ZIPFX_SOURCES src/dnd/MacPromiseDrag.mm)
endif()

add_executable(ZipFX ${ZIPFX_SOURCES})

target_include_directories(ZipFX PRIVATE
    src
    ${CMAKE_BINARY_DIR}/generated
    ${zlib_SOURCE_DIR}
    ${libzip_SOURCE_DIR}/lib
    ${libzip_BINARY_DIR}/lib
    ${libarchive_SOURCE_DIR}/libarchive
    ${CLI11_SOURCE_DIR}/include
    ${adflib_SOURCE_DIR}/src
    ${storm_SOURCE_DIR}/src
    ${libchdr_SOURCE_DIR}/include
)

target_compile_definitions(ZipFX PRIVATE
    LIBARCHIVE_STATIC
    $<$<CONFIG:Debug>:QT_QML_DEBUG>
)

target_link_libraries(ZipFX PRIVATE
    zip
    archive_static
    zlibstatic
    bit7z
    adf
    chdr-static
    storm
    Qt${QT_VERSION_MAJOR}::Widgets
    Qt${QT_VERSION_MAJOR}::Network
)

if(APPLE)
    target_link_libraries(ZipFX PRIVATE "-framework Cocoa")
    string(TIMESTAMP ZIPFX_YEAR "%Y")
    configure_file(
        "${CMAKE_SOURCE_DIR}/src/Info.plist.in"
        "${CMAKE_BINARY_DIR}/Info.plist"
        @ONLY
    )
    set_target_properties(ZipFX PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.zipfx.app"
        MACOSX_BUNDLE_BUNDLE_NAME "ZipFX"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_BINARY_DIR}/Info.plist"
        MACOSX_BUNDLE_ICON_FILE "AppIcon"
    )
    # Copy icon into the bundle
    set_source_files_properties("${CMAKE_SOURCE_DIR}/src/resources/AppIcon.icns"
        PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
    target_sources(ZipFX PRIVATE "${CMAKE_SOURCE_DIR}/src/resources/AppIcon.icns")
    # Copy translations into the bundle (also via RESOURCE)
    file(GLOB QM_FILES "${CMAKE_BINARY_DIR}/translations/*.qm")
    foreach(QM ${QM_FILES})
        set_source_files_properties("${QM}"
            PROPERTIES MACOSX_PACKAGE_LOCATION "Resources/translations")
        target_sources(ZipFX PRIVATE "${QM}")
    endforeach()
endif()

if(WIN32)
    target_link_libraries(ZipFX PRIVATE
        winmm comctl32 oleacc uxtheme
        rpcrt4 shlwapi version imm32
        ole32 oleaut32 powrprof
    )
    target_link_options(ZipFX PRIVATE "-Wl,--allow-multiple-definition")
    target_sources(ZipFX PRIVATE "${CMAKE_SOURCE_DIR}/src/resources/app.rc")
    set_target_properties(ZipFX PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

# ====================================================================
# Shell extension (Windows-only; pure Win32, no Qt)
# ====================================================================
if(WIN32)
    add_subdirectory(shellext)
endif()

# ====================================================================
# Tests
# ====================================================================
set(BUILD_TESTING ON CACHE BOOL "" FORCE)
if(BUILD_TESTING)
    find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Test)
    add_subdirectory(tests)
endif()

# ====================================================================
# Translations
# ====================================================================
# Copy pre-committed .qm files to build output (for macOS bundling and dev)
file(COPY ${CMAKE_SOURCE_DIR}/translations DESTINATION ${CMAKE_BINARY_DIR})
# Install next to the executable
install(DIRECTORY ${CMAKE_SOURCE_DIR}/translations/ DESTINATION translations
    FILES_MATCHING PATTERN "*.qm")
# Optionally regenerate .qm from .ts at build time
find_program(QT_LRELEASE lrelease PATHS "${Qt6_DIR}/../../bin" "${Qt6_DIR}/../../../bin")
if(QT_LRELEASE)
    file(GLOB TS_FILES "${CMAKE_SOURCE_DIR}/translations/*.ts")
    foreach(TS_FILE ${TS_FILES})
        get_filename_component(QM_FILE ${TS_FILE} NAME_WE)
        set(QM_OUT "${CMAKE_BINARY_DIR}/translations/${QM_FILE}.qm")
        add_custom_command(TARGET ZipFX POST_BUILD
            COMMAND ${QT_LRELEASE} ${TS_FILE} -qm ${QM_OUT}
            COMMENT "Generating ${QM_FILE}.qm"
        )
    endforeach()
endif()

# Copy app icon to build output (for development — not on macOS bundles)
if(NOT APPLE)
    add_custom_command(TARGET ZipFX POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/src/resources/AppIcon.png"
            "$<TARGET_FILE_DIR:ZipFX>/AppIcon.png"
        COMMENT "Copying AppIcon.png to output directory"
    )
endif()

install(TARGETS ZipFX
    RUNTIME DESTINATION bin
    BUNDLE DESTINATION .
)

# Install app icons next to the executable (Windows/macOS)
install(FILES
    "${CMAKE_SOURCE_DIR}/src/resources/AppIcon.png"
    "${CMAKE_SOURCE_DIR}/src/resources/AppIcon.ico"
    DESTINATION ".")

# ====================================================================
# Platform settings
# ====================================================================
if(WIN32 AND MSVC)
    set_target_properties(ZipFX PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

# Deploy Qt DLLs next to the executable
if(WIN32)
    find_program(QT_WINDEPLOYQT windeployqt
        PATHS "${Qt6_DIR}/../../bin"
        "${Qt6_DIR}/../../../bin"
        "${CMAKE_PREFIX_PATH}/bin"
    )
    if(QT_WINDEPLOYQT)
        add_custom_command(TARGET ZipFX POST_BUILD
            COMMAND ${QT_WINDEPLOYQT} --no-translations --no-compiler-runtime
                "$<TARGET_FILE_DIR:ZipFX>/ZipFX.exe"
            COMMENT "Running windeployqt..."
        )
    else()
        message(WARNING "windeployqt not found. "
            "Copy Qt DLLs manually or add Qt bin dir to PATH.")
    endif()
endif()

# Copy libzip.dll and libadf.dll next to the executable (shared libraries)
if(WIN32)
    add_custom_command(TARGET ZipFX POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${libzip_BINARY_DIR}/lib/libzip.dll"
            "$<TARGET_FILE_DIR:ZipFX>/"
        COMMENT "Copying libzip.dll"
    )
    add_custom_command(TARGET ZipFX POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${adflib_BINARY_DIR}/src/libadf.dll"
            "$<TARGET_FILE_DIR:ZipFX>/"
        COMMENT "Copying libadf.dll"
    )
    add_custom_command(TARGET ZipFX POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${StormLib_BINARY_DIR}/libStormLib.dll"
            "$<TARGET_FILE_DIR:ZipFX>/"
        COMMENT "Copying libStormLib.dll"
    )
endif()

# ====================================================================
# 7-Zip shared library  –  required by Bit7zEngine at runtime
# Searches in this order:
#   1. lib/<system>/<arch>/<name>        –  bundled with source
#   2. lib/<name>                        –  flat fallback
#   3. System install paths               –  developer machine
# ====================================================================
if(WIN32)
    set(_7ZIP_DLL_NAME "7z.dll")
else()
    set(_7ZIP_DLL_NAME "lib7z.so")
endif()

# Map CMake system/arch to our lib/ directory structure
if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    set(_7ZIP_PLATFORM "win")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    set(_7ZIP_PLATFORM "macos")
else()
    set(_7ZIP_PLATFORM "linux")
endif()

string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" _7ZIP_ARCH)
if(_7ZIP_ARCH MATCHES "amd64|x86_64")
    set(_7ZIP_ARCH "x64")
elseif(_7ZIP_ARCH MATCHES "aarch64|arm64")
    set(_7ZIP_ARCH "arm64")
endif()

# 1. Platform-specific bundle path
find_file(7ZIP_DLL
    NAMES "${_7ZIP_DLL_NAME}"
    PATHS "${CMAKE_SOURCE_DIR}/lib/${_7ZIP_PLATFORM}/${_7ZIP_ARCH}"
    NO_DEFAULT_PATH
)

# 2. Flat lib/ fallback
if(NOT 7ZIP_DLL)
    find_file(7ZIP_DLL
        NAMES "${_7ZIP_DLL_NAME}"
        PATHS "${CMAKE_SOURCE_DIR}/lib"
        NO_DEFAULT_PATH
    )
endif()

# 3. System install
if(NOT 7ZIP_DLL AND WIN32)
    find_file(7ZIP_DLL
        NAMES 7z.dll
        PATHS "C:/Program Files/7-Zip" "C:/Program Files (x86)/7-Zip"
        NO_DEFAULT_PATH
    )
endif()
if(NOT 7ZIP_DLL AND (APPLE OR UNIX))
    find_library(7ZIP_DLL NAMES lib7z.so 7z.so)
endif()

if(7ZIP_DLL)
    get_filename_component(_7ZIP_DLL_FILENAME "${7ZIP_DLL}" NAME)
    add_custom_command(TARGET ZipFX POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${7ZIP_DLL}"
            "$<TARGET_FILE_DIR:ZipFX>/${_7ZIP_DLL_FILENAME}"
        COMMENT "Copying ${_7ZIP_DLL_FILENAME}"
    )
    message(STATUS "Found 7z library at ${7ZIP_DLL}")
else()
    message(WARNING "7z.dll/lib7z.so not found. "
        "Place it in ${CMAKE_SOURCE_DIR}/lib/${_7ZIP_PLATFORM}/${_7ZIP_ARCH}/ "
        "or install 7-Zip. Bit7zEngine will be unavailable.")
endif()

# ====================================================================
# ====================================================================
# CPack
# ====================================================================
set(CPACK_PACKAGE_NAME             "ZipFX")
set(CPACK_PACKAGE_VERSION          "${PROJECT_VERSION}")
set(CPACK_PACKAGE_VENDOR           "Krusher")
set(CPACK_PACKAGE_HOMEPAGE_URL     "https://github.com/axelei/ZipFX")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Multiplatform archiver for power users")
set(CPACK_RESOURCE_FILE_LICENSE    "${CMAKE_SOURCE_DIR}/LICENSE")

if(WIN32)
    set(CPACK_GENERATOR "NSIS;ZIP")
    set(CPACK_NSIS_DISPLAY_NAME "ZipFX")
    set(CPACK_NSIS_INSTALL_ROOT "$PROGRAMFILES64")
elseif(APPLE)
    set(CPACK_GENERATOR "DragNDrop;TGZ")
    set(CPACK_DMG_VOLUME_NAME "ZipFX")
else()
    set(CPACK_GENERATOR "DEB;RPM;TGZ")
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "ZipFX Team")
    set(CPACK_DEBIAN_FILE_NAME "ZipFX-${CPACK_PACKAGE_VERSION}.deb")
    set(CPACK_RPM_PACKAGE_RELEASE "1")
    set(CPACK_RPM_PACKAGE_LICENSE "GPL-3.0-or-later")
endif()

include(CPack)

# ── Linux: desktop, MIME & icon registration ──────────────────────
if(UNIX AND NOT APPLE)
    install(FILES "${CMAKE_SOURCE_DIR}/src/resources/zipfx.desktop"
        DESTINATION "share/applications")
    install(FILES "${CMAKE_SOURCE_DIR}/src/resources/zipfx-mime.xml"
        DESTINATION "share/mime/packages")
    install(FILES "${CMAKE_SOURCE_DIR}/src/resources/AppIcon.png"
        DESTINATION "share/icons/hicolor/16x16/apps"
        RENAME "zipfx.png")
    install(FILES "${CMAKE_SOURCE_DIR}/src/resources/AppIcon.png"
        DESTINATION "share/icons/hicolor/22x22/apps"
        RENAME "zipfx.png")
    install(FILES "${CMAKE_SOURCE_DIR}/src/resources/AppIcon.png"
        DESTINATION "share/icons/hicolor/32x32/apps"
        RENAME "zipfx.png")
    install(FILES "${CMAKE_SOURCE_DIR}/src/resources/AppIcon.png"
        DESTINATION "share/icons/hicolor/48x48/apps"
        RENAME "zipfx.png")
    install(FILES "${CMAKE_SOURCE_DIR}/src/resources/AppIcon.png"
        DESTINATION "share/icons/hicolor/64x64/apps"
        RENAME "zipfx.png")
    install(FILES "${CMAKE_SOURCE_DIR}/src/resources/AppIcon.png"
        DESTINATION "share/icons/hicolor/128x128/apps"
        RENAME "zipfx.png")
    install(FILES "${CMAKE_SOURCE_DIR}/src/resources/AppIcon.png"
        DESTINATION "share/icons/hicolor/256x256/apps"
        RENAME "zipfx.png")
    install(FILES "${CMAKE_SOURCE_DIR}/src/resources/AppIcon.png"
        DESTINATION "share/icons/hicolor/scalable/apps"
        RENAME "zipfx.png")

    # AppStream metainfo
    install(FILES "${CMAKE_SOURCE_DIR}/src/resources/com.zipfx.app.metainfo.xml"
        DESTINATION "share/metainfo")

    # Post-install: update XDG databases
    include(CMakePackageConfigHelpers)
    install(CODE "
        execute_process(COMMAND update-desktop-database -q
            COMMAND_ECHO STDOUT)
        execute_process(COMMAND update-mime-database ${CMAKE_INSTALL_PREFIX}/share/mime
            COMMAND_ECHO STDOUT)
    ")
endif()
