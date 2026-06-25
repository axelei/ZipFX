# Override FindLibLZMA to use our already-fetched liblzma target.

if(TARGET liblzma)
    set(LIBLZMA_FOUND TRUE PARENT_SCOPE)
    set(LIBLZMA_HAS_AUTO_DECODER TRUE PARENT_SCOPE)
    set(LIBLZMA_HAS_EASY_ENCODER TRUE PARENT_SCOPE)
    set(LIBLZMA_HAS_LZMA_PRESET TRUE PARENT_SCOPE)
    get_target_property(_lzma_inc liblzma INTERFACE_INCLUDE_DIRECTORIES)
    if(_lzma_inc)
        set(LIBLZMA_INCLUDE_DIRS "${_lzma_inc}" PARENT_SCOPE)
    else()
        set(LIBLZMA_INCLUDE_DIRS "${liblzma_SOURCE_DIR}/src/liblzma/api" PARENT_SCOPE)
    endif()
    set(LIBLZMA_LIBRARIES "liblzma" PARENT_SCOPE)
    set(LIBLZMA_LIBRARY "liblzma" PARENT_SCOPE)

    if(NOT TARGET LibLZMA::LibLZMA)
        add_library(LibLZMA::LibLZMA ALIAS liblzma)
    endif()
    return()
endif()

include("${CMAKE_ROOT}/Modules/FindLibLZMA.cmake")
