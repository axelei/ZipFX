# Override FindLZ4 to use our already-fetched lz4_static target.

if(TARGET lz4_static)
    set(LZ4_FOUND TRUE PARENT_SCOPE)
    get_target_property(_lz4_inc lz4_static INTERFACE_INCLUDE_DIRECTORIES)
    if(_lz4_inc)
        set(LZ4_INCLUDE_DIR "${_lz4_inc}" PARENT_SCOPE)
        set(LZ4_INCLUDE_DIRS "${_lz4_inc}" PARENT_SCOPE)
    else()
        set(LZ4_INCLUDE_DIR "${lz4_SOURCE_DIR}/lib" PARENT_SCOPE)
        set(LZ4_INCLUDE_DIRS "${lz4_SOURCE_DIR}/lib" PARENT_SCOPE)
    endif()
    set(LZ4_LIBRARY "lz4_static" PARENT_SCOPE)
    set(LZ4_LIBRARIES "lz4_static" PARENT_SCOPE)

    if(NOT TARGET LZ4::LZ4)
        add_library(LZ4::LZ4 ALIAS lz4_static)
    endif()
    return()
endif()

# Fall back to pkg-config / standard search
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(LZ4 QUIET liblz4)
endif()
