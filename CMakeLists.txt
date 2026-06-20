cmake_minimum_required(VERSION 3.20)
project(ZipFX VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

include(FetchContent)

# -------------------------------------------------------------------
# wxWidgets
# -------------------------------------------------------------------
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

# -------------------------------------------------------------------
# Our executable
# -------------------------------------------------------------------
add_executable(ZipFX
    src/main.cpp
)

target_include_directories(ZipFX PRIVATE
    ${wxwidgets_SOURCE_DIR}/include
    ${wxwidgets_BINARY_DIR}/include
)

target_link_libraries(ZipFX PRIVATE wxmono)

if(WIN32)
    target_link_libraries(ZipFX PRIVATE
        winmm comctl32 uuid oleacc uxtheme
        rpcrt4 shlwapi version imm32
    )
endif()
