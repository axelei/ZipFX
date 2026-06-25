# Override FindZstd to use our already-fetched libzstd_static target.

if(TARGET libzstd_static)
    set(ZSTD_FOUND TRUE PARENT_SCOPE)
    get_target_property(_zstd_inc libzstd_static INTERFACE_INCLUDE_DIRECTORIES)
    if(_zstd_inc)
        set(ZSTD_INCLUDE_DIR "${_zstd_inc}" PARENT_SCOPE)
        set(ZSTD_INCLUDE_DIRS "${_zstd_inc}" PARENT_SCOPE)
    else()
        set(ZSTD_INCLUDE_DIR "${zstd_SOURCE_DIR}/lib" PARENT_SCOPE)
        set(ZSTD_INCLUDE_DIRS "${zstd_SOURCE_DIR}/lib" PARENT_SCOPE)
    endif()
    set(ZSTD_LIBRARY "libzstd_static" PARENT_SCOPE)
    set(ZSTD_LIBRARIES "libzstd_static" PARENT_SCOPE)

    if(NOT TARGET zstd::libzstd_static)
        add_library(zstd::libzstd_static ALIAS libzstd_static)
    endif()
    return()
endif()

# Fall back to pkg-config / standard search
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(ZSTD QUIET libzstd)
endif()
