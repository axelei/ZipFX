enable_testing()

# Engine sources needed by tests (shared across all test targets)
set(ENGINE_SOURCES
    ${CMAKE_SOURCE_DIR}/src/engine/FileSignature.cpp
    ${CMAKE_SOURCE_DIR}/src/engine/ArchiveEngine.cpp
    ${CMAKE_SOURCE_DIR}/src/engine/ArchiveEngineFactory.cpp
    ${CMAKE_SOURCE_DIR}/src/engine/ZipEngine.cpp
    ${CMAKE_SOURCE_DIR}/src/engine/TarGzEngine.cpp
    ${CMAKE_SOURCE_DIR}/src/engine/LibarchiveEngine.cpp
    ${CMAKE_SOURCE_DIR}/src/engine/Bit7zEngine.cpp
    ${CMAKE_SOURCE_DIR}/src/engine/FlatArchiveEngine.cpp
    ${CMAKE_SOURCE_DIR}/src/engine/WadEngine.cpp
    ${CMAKE_SOURCE_DIR}/src/engine/PakEngine.cpp
    ${CMAKE_SOURCE_DIR}/src/engine/GrpEngine.cpp
    ${CMAKE_SOURCE_DIR}/src/engine/HogEngine.cpp
    ${CMAKE_SOURCE_DIR}/src/engine/VpkEngine.cpp
    ${CMAKE_SOURCE_DIR}/src/engine/GobEngine.cpp
    ${CMAKE_SOURCE_DIR}/src/engine/RffEngine.cpp
    ${CMAKE_SOURCE_DIR}/src/engine/BigEngine.cpp
    ${CMAKE_SOURCE_DIR}/src/engine/PodEngine.cpp
    ${CMAKE_SOURCE_DIR}/src/engine/RarEngine.cpp
    ${CMAKE_SOURCE_DIR}/src/engine/Iso9660Reader.cpp
    ${CMAKE_SOURCE_DIR}/src/engine/IsoEngine.cpp
    ${CMAKE_SOURCE_DIR}/src/engine/CdiEngine.cpp
    ${CMAKE_SOURCE_DIR}/src/engine/GdiEngine.cpp
    ${CMAKE_SOURCE_DIR}/src/engine/DskEngine.cpp
    ${CMAKE_SOURCE_DIR}/src/engine/SsdEngine.cpp
    ${CMAKE_SOURCE_DIR}/src/engine/AtrEngine.cpp
    ${CMAKE_SOURCE_DIR}/src/engine/D64Engine.cpp
    ${CMAKE_SOURCE_DIR}/src/engine/FatEngine.cpp
    ${CMAKE_SOURCE_DIR}/src/engine/ChdEngine.cpp
    ${CMAKE_SOURCE_DIR}/src/engine/MpqEngine.cpp
    ${CMAKE_SOURCE_DIR}/src/engine/AdfEngine.cpp
    ${CMAKE_SOURCE_DIR}/src/engine/BsaEngine.cpp
    ${CMAKE_SOURCE_DIR}/src/engine/BrotliEngine.cpp
    ${CMAKE_SOURCE_DIR}/src/engine/ModEngine.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/FileListModel.cpp
    ${CMAKE_SOURCE_DIR}/src/dnd/TempCleanup.cpp
)

set(CMAKE_AUTOMOC ON)

set(TEST_LIBS
    zip
    archive_static
    zlibstatic
    adf
    chdr-static
    bit7z
    storm
    brotlidec
    brotlicommon
    xmp_static
    Qt${QT_VERSION_MAJOR}::Widgets
    Qt${QT_VERSION_MAJOR}::Test
)

if(WIN32)
    list(APPEND TEST_LIBS
        winmm comctl32 oleacc uxtheme
        rpcrt4 shlwapi version imm32
        ole32 oleaut32 powrprof
    )
endif()

# Collect DLLs that need to be next to test executables
set(TEST_DLLS "")
if(WIN32)
    list(APPEND TEST_DLLS
        "${libzip_BINARY_DIR}/lib/libzip.dll"
    )
    if(7ZIP_DLL)
        list(APPEND TEST_DLLS "${7ZIP_DLL}")
    endif()
    if(EXISTS "${StormLib_BINARY_DIR}/libStormLib.dll")
        list(APPEND TEST_DLLS "${StormLib_BINARY_DIR}/libStormLib.dll")
    endif()
    if(EXISTS "${adflib_BINARY_DIR}/src/libadf.dll")
        list(APPEND TEST_DLLS "${adflib_BINARY_DIR}/src/libadf.dll")
    endif()
endif()

macro(add_zipfx_test TEST_NAME)
    add_executable(${TEST_NAME}
        ${TEST_NAME}.cpp
        TestUtils.cpp
        ${ENGINE_SOURCES}
    )
    set_target_properties(${TEST_NAME} PROPERTIES WIN32_EXECUTABLE FALSE)
    target_include_directories(${TEST_NAME} PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_BINARY_DIR}/generated
        ${libzip_SOURCE_DIR}/lib
        ${libzip_BINARY_DIR}/lib
        ${libarchive_SOURCE_DIR}/libarchive
        ${adflib_SOURCE_DIR}
        ${storm_SOURCE_DIR}/src
        ${libchdr_SOURCE_DIR}/include
        ${brotli_SOURCE_DIR}/c/include
        ${CLI11_SOURCE_DIR}/include
        ${libxmp_SOURCE_DIR}/include
    )
    target_compile_definitions(${TEST_NAME} PRIVATE
        LIBARCHIVE_STATIC
        $<$<CONFIG:Debug>:QT_QML_DEBUG>
    )
    target_link_libraries(${TEST_NAME} PRIVATE ${TEST_LIBS})

    # Copy required DLLs next to the test executable
    if(WIN32)
        foreach(DLL ${TEST_DLLS})
            add_custom_command(TARGET ${TEST_NAME} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${DLL}" "$<TARGET_FILE_DIR:${TEST_NAME}>/"
                COMMENT "Copying ${DLL}"
            )
        endforeach()
    endif()

    add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
    # Find Qt paths for runtime environment
    get_target_property(_QtWidgets_LOC Qt6::Widgets IMPORTED_LOCATION)
    get_filename_component(_QT_BIN_DIR "${_QtWidgets_LOC}" DIRECTORY)
    get_filename_component(_QT_DIR "${_QT_BIN_DIR}" DIRECTORY)
    if(WIN32)
        set_tests_properties(${TEST_NAME} PROPERTIES
            ENVIRONMENT "PATH=${_QT_BIN_DIR}\;${CMAKE_BINARY_DIR}/tests\;C:/Program Files/7-Zip\;$ENV{PATH};QT_PLUGIN_PATH=${_QT_DIR}/plugins")
    endif()
endmacro()

add_zipfx_test(tst_FileSignature)
add_zipfx_test(tst_ArchiveEngineFactory)
add_zipfx_test(tst_ZipEngine)
add_zipfx_test(tst_TarGzEngine)
add_zipfx_test(tst_Bit7zEngine)
add_zipfx_test(tst_LibarchiveEngine)
add_zipfx_test(tst_FileListModel)
add_zipfx_test(tst_CliHandler)
add_zipfx_test(tst_FlatArchiveEngines)
add_zipfx_test(tst_MpqEngine)
add_zipfx_test(tst_ModEngine)
add_zipfx_test(tst_TempCleanup)
