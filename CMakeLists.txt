cmake_minimum_required(VERSION 3.20)
project(ZipFX VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_AUTOMOC ON)

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
set(ZLIB_INSTALL      OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(zlib)

# Custom FindZLIB that aliases to our zlibstatic target
list(INSERT CMAKE_MODULE_PATH 0 "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
set(HAVE_SNPRINTF TRUE CACHE INTERNAL "")

# ====================================================================
# libzip  –  ZIP read/write (in-place delete/add)
# ====================================================================
set(ENABLE_COMMONCRYPTO OFF CACHE BOOL "" FORCE)
set(ENABLE_GNUTLS      OFF CACHE BOOL "" FORCE)
set(ENABLE_MBEDTLS     OFF CACHE BOOL "" FORCE)
set(ENABLE_OPENSSL     OFF CACHE BOOL "" FORCE)
set(BUILD_TOOLS        OFF CACHE BOOL "" FORCE)
set(BUILD_REGRESS      OFF CACHE BOOL "" FORCE)
set(BUILD_EXAMPLES     OFF CACHE BOOL "" FORCE)
set(BUILD_DOC          OFF CACHE BOOL "" FORCE)
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

# ====================================================================
# libarchive  –  7z read/write + RAR read
# ====================================================================
set(ENABLE_ZLIB         OFF CACHE BOOL "" FORCE)
set(ENABLE_BZip2        OFF CACHE BOOL "" FORCE)
set(ENABLE_LZMA         OFF CACHE BOOL "" FORCE)
set(ENABLE_LZ4          OFF CACHE BOOL "" FORCE)
set(ENABLE_ZSTD         OFF CACHE BOOL "" FORCE)
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

target_compile_definitions(archive_static PRIVATE LIBARCHIVE_STATIC)

# ====================================================================
# Qt6  –  cross-platform UI
# ====================================================================
# Common default installation paths per platform.
# For CI (GitHub Actions), the `jurplel/install-qt-action` sets
# CMAKE_PREFIX_PATH automatically — no changes needed.
if(WIN32)
    list(APPEND CMAKE_PREFIX_PATH "C:/Qt/6.11.1/mingw_64")
elseif(APPLE)
    list(APPEND CMAKE_PREFIX_PATH "/usr/local/opt/qt" "/opt/homebrew/opt/qt")
endif()
find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets)

# ====================================================================
# Our executable
# ====================================================================
set(ZIPFX_SOURCES
    src/main.cpp
    src/ui/MainWindow.cpp
    src/ui/FileListModel.cpp
    src/ui/CreateArchiveDialog.cpp
    src/engine/ArchiveEngine.cpp
    src/engine/ArchiveEngineFactory.cpp
    src/engine/ZipEngine.cpp
    src/engine/TarGzEngine.cpp
    src/engine/LibarchiveEngine.cpp
    src/engine/SevenZipEngine.cpp
    src/engine/RarEngine.cpp
)

if(WIN32)
    list(APPEND ZIPFX_SOURCES src/dnd/VirtualFileDataObject.cpp)
endif()

add_executable(ZipFX ${ZIPFX_SOURCES})

target_include_directories(ZipFX PRIVATE
    src
    ${libzip_SOURCE_DIR}/lib
    ${libzip_BINARY_DIR}/lib
    ${libarchive_SOURCE_DIR}/libarchive
)

target_compile_definitions(ZipFX PRIVATE
    LIBARCHIVE_STATIC
    $<$<CONFIG:Debug>:QT_QML_DEBUG>
)

target_link_libraries(ZipFX PRIVATE
    zip
    archive_static
    zlibstatic
    Qt${QT_VERSION_MAJOR}::Widgets
)

if(WIN32)
    target_link_libraries(ZipFX PRIVATE
        winmm comctl32 uuid oleacc uxtheme
        rpcrt4 shlwapi version imm32
        ole32 oleaut32 powrprof
    )
endif()

# ====================================================================
# Translations  –  generate .qm from .ts at build time
# ====================================================================
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
    # Copy .qm files to build output (for development)
    file(COPY ${CMAKE_SOURCE_DIR}/translations DESTINATION ${CMAKE_BINARY_DIR})
    # Also install next to the executable
    install(DIRECTORY ${CMAKE_SOURCE_DIR}/translations/ DESTINATION translations
        FILES_MATCHING PATTERN "*.qm")
endif()

# ====================================================================
# Platform settings
# ====================================================================
if(WIN32 AND MSVC)
    set_target_properties(ZipFX PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

# Copy libzip.dll next to the executable (shared library)
if(WIN32)
    add_custom_command(TARGET ZipFX POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${libzip_BINARY_DIR}/lib/libzip.dll"
            "$<TARGET_FILE_DIR:ZipFX>/"
        COMMENT "Copying libzip.dll"
    )
endif()

# ====================================================================
# ====================================================================
# CPack
# ====================================================================
set(CPACK_PACKAGE_NAME             "ZipFX")
set(CPACK_PACKAGE_VERSION          "${PROJECT_VERSION}")
set(CPACK_PACKAGE_VENDOR           "ZipFX Team")
set(CPACK_PACKAGE_HOMEPAGE_URL     "https://github.com/axelei/ZipFX")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "A cross-platform archive manager")
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
