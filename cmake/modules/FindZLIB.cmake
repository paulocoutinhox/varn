# When madler/zlib is added via CPM (target zlibstatic), satisfy find_package(ZLIB) like CMake's
# FindZLIB so libzip and Varn see ZLIB::ZLIB. Otherwise delegate to upstream FindZLIB.cmake.
if(TARGET zlibstatic)
    if(NOT TARGET ZLIB::ZLIB)
        add_library(ZLIB::ZLIB ALIAS zlibstatic)
    endif()
    get_target_property(_varn_zlib_includes zlibstatic INTERFACE_INCLUDE_DIRECTORIES)
    if(_varn_zlib_includes STREQUAL "_varn_zlib_includes-NOTFOUND" OR NOT _varn_zlib_includes)
        message(FATAL_ERROR "varn FindZLIB: zlibstatic has no INTERFACE_INCLUDE_DIRECTORIES")
    endif()
    set(ZLIB_INCLUDE_DIRS "${_varn_zlib_includes}")
    foreach(_varn_d IN LISTS _varn_zlib_includes)
        if(EXISTS "${_varn_d}/zlib.h")
            set(ZLIB_INCLUDE_DIR "${_varn_d}")
            break()
        endif()
    endforeach()
    if(NOT ZLIB_INCLUDE_DIR)
        list(GET _varn_zlib_includes 0 ZLIB_INCLUDE_DIR)
    endif()

    set(ZLIB_LIBRARY zlibstatic)
    set(ZLIB_LIBRARIES zlibstatic)
    # Keep in sync with CPMAddPackage zlib GIT_TAG in cmake/cpm/varn-dependencies.cmake.
    set(ZLIB_VERSION_STRING "1.3.1")
    set(ZLIB_VERSION "${ZLIB_VERSION_STRING}")
    set(ZLIB_VERSION_MAJOR "1")
    set(ZLIB_VERSION_MINOR "3")
    set(ZLIB_VERSION_PATCH "1")

    include(FindPackageHandleStandardArgs)
    # Do not put ZLIB_LIBRARY in REQUIRED_VARS: it is the target name zlibstatic, not a file path,
    # and FPHSA / version checks can leave ZLIB_FOUND unset on some toolchains (e.g. Emscripten).
    find_package_handle_standard_args(ZLIB REQUIRED_VARS ZLIB_INCLUDE_DIR
        VERSION_VAR ZLIB_VERSION)
    return()
endif()

include(${CMAKE_ROOT}/Modules/FindZLIB.cmake)
