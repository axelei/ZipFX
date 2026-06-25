# Override FindBZip2 to use our already-fetched bz2_static target.

if(TARGET bz2_static)
    set(BZIP2_FOUND TRUE PARENT_SCOPE)
    set(BZIP2_INCLUDE_DIR "${bzip2_SOURCE_DIR}" PARENT_SCOPE)
    set(BZIP2_INCLUDE_DIRS "${bzip2_SOURCE_DIR}" PARENT_SCOPE)
    set(BZIP2_LIBRARIES "bz2_static" PARENT_SCOPE)
    set(BZIP2_LIBRARY "bz2_static" PARENT_SCOPE)

    if(NOT TARGET BZip2::BZip2)
        add_library(BZip2::BZip2 ALIAS bz2_static)
    endif()
    return()
endif()

include("${CMAKE_ROOT}/Modules/FindBZip2.cmake")
