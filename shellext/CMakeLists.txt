cmake_minimum_required(VERSION 3.20)

# ── Shell extension DLL (Windows-only, no Qt) ──────────────────────────────
# COM in-process server; loaded by Explorer — must not link Qt.
# Disable MOC: no Qt headers in this target.
set(CMAKE_AUTOMOC OFF)

add_library(ZipFXShellExt SHARED
    ZipFXShellExt.cpp
    ZipFXShellExt.h
)

target_compile_options(ZipFXShellExt PRIVATE
    -Wall -Wextra
)

# Exported COM entry points need to survive MinGW's name mangling / --kill-at
# strips the stdcall @N suffix so regsvr32 / LoadLibrary can find them.
target_link_options(ZipFXShellExt PRIVATE
    "-Wl,--kill-at"
    "-Wl,--allow-multiple-definition"
)

target_link_libraries(ZipFXShellExt PRIVATE
    shlwapi shell32 ole32 uuid
)

set_target_properties(ZipFXShellExt PROPERTIES
    OUTPUT_NAME "ZipFXShellExt"
    PREFIX      ""          # no lib prefix on Windows
)

# Copy the DLL next to ZipFX.exe so it finds ZipFX.exe via sibling lookup
add_custom_command(TARGET ZipFXShellExt POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:ZipFXShellExt>"
        "$<TARGET_FILE_DIR:ZipFX>/ZipFXShellExt.dll"
    COMMENT "Copying ZipFXShellExt.dll to ZipFX output directory"
    DEPENDS ZipFX
)

install(TARGETS ZipFXShellExt RUNTIME DESTINATION ".")
