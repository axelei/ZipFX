cmake_minimum_required(VERSION 3.20)
project(ZipFX VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

include(FetchContent)

# ====================================================================
# wxWidgets  –  UI framework
# ====================================================================
set(wxBUILD_SHARED    OFF CACHE BOOL "" FORCE)
set(wxBUILD_MONOLITHIC ON CACHE BOOL "" FORCE)
set(wxBUILD_SAMPLES   OFF CACHE BOOL "" FORCE)
set(wxBUILD_DEMOS     OFF CACHE BOOL "" FORCE)
set(wxBUILD_TESTS     OFF CACHE BOOL "" FORCE)
set(wxUSE_OPENGL      OFF CACHE BOOL "" FORCE)
set(wxUSE_WEBVIEW     OFF CACHE BOOL "" FORCE)
set(wxUSE_MEDIACTRL   OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    wxwidgets
    GIT_REPOSITORY https://github.com/wxWidgets/wxWidgets.git
    GIT_TAG        v3.2.6
    GIT_SHALLOW    TRUE
)

FetchContent_MakeAvailable(wxwidgets)

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

# libzip's FindZLIB needs the actual library file at configure time.
# Point it at our fetched zlib (built later, but the target exists now).
if(TARGET zlibstatic)
    # Get the actual output path for the zlib library (handles all generators)
    get_target_property(_zlib_type zlibstatic TYPE)
    if(_zlib_type STREQUAL "STATIC_LIBRARY")
        get_target_property(_zlib_out zlibstatic ARCHIVE_OUTPUT_DIRECTORY)
        if(NOT _zlib_out)
            set(_zlib_out "${CMAKE_CURRENT_BINARY_DIR}")
        endif()
    endif()

    set(ZLIB_FOUND       TRUE                              CACHE INTERNAL "")
    set(ZLIB_INCLUDE_DIR "${zlib_SOURCE_DIR}" "${zlib_BINARY_DIR}" CACHE PATH "")
    set(ZLIB_LIBRARY     "zlibstatic"                      CACHE STRING   "")
    set(ZLIB_LIBRARIES   "zlibstatic"                      CACHE STRING   "")
endif()

# ====================================================================
# libzip  –  ZIP read/write (in-place delete/add, no full rewrite)
# ====================================================================
set(ENABLE_COMMONCRYPTO OFF CACHE BOOL "" FORCE)
set(ENABLE_GNUTLS      OFF CACHE BOOL "" FORCE)
set(ENABLE_MBEDTLS     OFF CACHE BOOL "" FORCE)
set(ENABLE_OPENSSL     OFF CACHE BOOL "" FORCE)
set(BUILD_TOOLS        OFF CACHE BOOL "" FORCE)
set(BUILD_REGRESS      OFF CACHE BOOL "" FORCE)
set(BUILD_EXAMPLES     OFF CACHE BOOL "" FORCE)
set(BUILD_DOC          OFF CACHE BOOL "" FORCE)

# Tell libzip that snprintf exists (MSVC 2015+) to avoid macro conflict in compat.h
set(HAVE_SNPRINTF TRUE CACHE INTERNAL "Have snprintf")
set(CMAKE_REQUIRED_DEFINITIONS "-DHAVE_SNPRINTF=1")

# Help libzip find our zlib target (also handles MSVC Debug config via target name)
set(ZLIB_LIBRARIES "zlibstatic" CACHE STRING "Zlib library")

FetchContent_Declare(
    libzip
    GIT_REPOSITORY https://github.com/nih-at/libzip.git
    GIT_TAG        v1.11.3
    GIT_SHALLOW    TRUE
)

FetchContent_MakeAvailable(libzip)

# ====================================================================
# libarchive  –  7z read/write + RAR read
#               (all optional external deps disabled)
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

# ====================================================================
# Our executable
# ====================================================================
set(ZIPFX_SOURCES
    src/main.cpp
    src/ui/MainFrame.cpp
    src/ui/FileListPanel.cpp
    src/ui/CreateArchiveDialog.cpp
    src/ui/ExtractionWorker.cpp
    src/ui/ExtractionProgressDialog.cpp
    src/dnd/VirtualFileDataObject.cpp
    src/engine/ArchiveEngineFactory.cpp
    src/engine/ZipEngine.cpp
    src/engine/TarGzEngine.cpp
    src/engine/SevenZipEngine.cpp
    src/engine/RarEngine.cpp
)

add_executable(ZipFX ${ZIPFX_SOURCES})

target_include_directories(ZipFX PRIVATE
    src
    ${libzip_SOURCE_DIR}/lib
    ${libzip_BINARY_DIR}/lib
    ${libarchive_SOURCE_DIR}/libarchive
    ${wxwidgets_SOURCE_DIR}/include
    ${wxwidgets_BINARY_DIR}/include
)

target_compile_definitions(ZipFX PRIVATE LIBARCHIVE_STATIC)

target_link_libraries(ZipFX PRIVATE
    wxmono
    archive_static
    zlibstatic
    zip
)

if(WIN32)
    target_link_libraries(ZipFX PRIVATE
        winmm comctl32 uuid oleacc uxtheme
        rpcrt4 shlwapi version imm32
        ole32 oleaut32 powrprof
    )
endif()

# Windows GUI subsystem (required by wxWidgets on MSVC)
set_target_properties(ZipFX PROPERTIES WIN32_EXECUTABLE TRUE)

# Copy assets and locale next to the build output
file(COPY ${CMAKE_SOURCE_DIR}/src/assets DESTINATION ${CMAKE_BINARY_DIR})
file(COPY ${CMAKE_SOURCE_DIR}/locale    DESTINATION ${CMAKE_BINARY_DIR})
add_custom_command(TARGET ZipFX POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/src/assets
        $<TARGET_FILE_DIR:ZipFX>/assets
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/locale
        $<TARGET_FILE_DIR:ZipFX>/locale
)

# ====================================================================
# Install rules
# ====================================================================
install(TARGETS ZipFX
    RUNTIME DESTINATION .
    BUNDLE  DESTINATION .
)

install(DIRECTORY ${CMAKE_SOURCE_DIR}/src/assets/ DESTINATION assets
    USE_SOURCE_PERMISSIONS
)

install(DIRECTORY ${CMAKE_SOURCE_DIR}/locale/ DESTINATION locale
    USE_SOURCE_PERMISSIONS
)

# ====================================================================
# CPack  –  Multi-platform installer generation
# ====================================================================
set(CPACK_PACKAGE_NAME             "ZipFX")
set(CPACK_PACKAGE_VERSION          "${PROJECT_VERSION}")
set(CPACK_PACKAGE_VENDOR           "ZipFX Team")
set(CPACK_PACKAGE_HOMEPAGE_URL     "https://github.com/axelei/ZipFX")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "A cross-platform archive manager (ZIP, 7z, RAR, TAR.GZ)")
set(CPACK_RESOURCE_FILE_LICENSE    "${CMAKE_SOURCE_DIR}/LICENSE")

if(WIN32)
    # NSIS  –  Windows installer  (requires NSIS installed)
    set(CPACK_GENERATOR "NSIS;ZIP")
    set(CPACK_NSIS_DISPLAY_NAME "ZipFX")
    set(CPACK_NSIS_INSTALL_ROOT "$PROGRAMFILES64")
    set(CPACK_NSIS_MENU_LINKS
        "https://github.com/axelei/ZipFX" "ZipFX Homepage")
elseif(APPLE)
    # DragNDrop  –  macOS .dmg  +  portable .tar.gz
    set(CPACK_GENERATOR "DragNDrop;TGZ")
    set(CPACK_DMG_VOLUME_NAME "ZipFX")
    set(CPACK_DMG_DISABLE_APPLICATIONS_SYMLINK ON)
else()
    # DEB / RPM / TGZ  –  Linux
    set(CPACK_GENERATOR "DEB;RPM;TGZ")
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "ZipFX Team")
    set(CPACK_DEBIAN_FILE_NAME    "ZipFX-${CPACK_PACKAGE_VERSION}.deb")
    set(CPACK_RPM_PACKAGE_RELEASE "1")
    set(CPACK_RPM_PACKAGE_LICENSE "GPL-3.0-or-later")
endif()

include(CPack)
