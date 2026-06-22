# Override FindZLIB to use our already-fetched zlibstatic target.
# This prevents libzip from searching for a library file on disk.

if(TARGET zlibstatic)
    set(ZLIB_FOUND TRUE PARENT_SCOPE)
    get_target_property(_zlib_inc zlibstatic INTERFACE_INCLUDE_DIRECTORIES)
    if(_zlib_inc)
        set(ZLIB_INCLUDE_DIRS "${_zlib_inc}" PARENT_SCOPE)
    else()
        set(ZLIB_INCLUDE_DIRS "${zlib_SOURCE_DIR};${zlib_BINARY_DIR}" PARENT_SCOPE)
    endif()
    set(ZLIB_LIBRARIES "zlibstatic" PARENT_SCOPE)
    set(ZLIB_LIBRARY "zlibstatic" PARENT_SCOPE)

    # Create the IMPORTED target that libzip expects
    if(NOT TARGET ZLIB::ZLIB)
        add_library(ZLIB::ZLIB ALIAS zlibstatic)
    endif()
    return()
endif()

# Fall back to the standard search
include("${CMAKE_ROOT}/Modules/FindZLIB.cmake")
