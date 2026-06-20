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
# miniz  –  ZIP read/write (single-file C library, no CMakeLists)
# ====================================================================
FetchContent_Declare(
    miniz
    GIT_REPOSITORY https://github.com/richgel999/miniz.git
    GIT_TAG        3.0.2
    GIT_SHALLOW    TRUE
)

FetchContent_GetProperties(miniz)
if(NOT miniz_POPULATED)
    FetchContent_Populate(miniz)
endif()

# ====================================================================
# zlib  –  gzip compression for .tar.gz support
# ====================================================================
FetchContent_Declare(
    zlib
    GIT_REPOSITORY https://github.com/madler/zlib.git
    GIT_TAG        v1.3.1
    GIT_SHALLOW    TRUE
)

set(ZLIB_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(zlib)

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
    src/engine/ArchiveEngineFactory.cpp
    src/engine/ZipEngine.cpp
    src/engine/TarGzEngine.cpp
    src/engine/SevenZipEngine.cpp
    src/engine/RarEngine.cpp
    ${miniz_SOURCE_DIR}/miniz.c
)

add_executable(ZipFX ${ZIPFX_SOURCES})

target_include_directories(ZipFX PRIVATE
    src
    ${miniz_SOURCE_DIR}
    ${wxwidgets_SOURCE_DIR}/include
    ${wxwidgets_BINARY_DIR}/include
)

target_link_libraries(ZipFX PRIVATE
    wxmono
    archive
    zlib
)

if(WIN32)
    target_link_libraries(ZipFX PRIVATE
        winmm comctl32 uuid oleacc uxtheme
        rpcrt4 shlwapi version imm32
    )
endif()

# Copy assets directory next to the executable (so PNGs ship with it)
file(COPY ${CMAKE_SOURCE_DIR}/src/assets DESTINATION ${CMAKE_BINARY_DIR})
