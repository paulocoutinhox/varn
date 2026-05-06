include_guard(GLOBAL)

if(NOT VARN_FFI_DRIVER STREQUAL "LIBFFI")
    return()
endif()

if(EMSCRIPTEN OR CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
    message(FATAL_ERROR
        "VARN_FFI_DRIVER=LIBFFI cannot be used when targeting Emscripten/WebAssembly. "
        "Use VARN_FFI_DRIVER=DUMMY (CMakeLists.txt should force this when CMAKE_SYSTEM_NAME is Emscripten).")
endif()

set(VARN_LIBFFI_VERSION "3.5.2")
set(VARN_LIBFFI_VERSION_NUMBER 30502)

CPMAddPackage(
    NAME varn_libffi_upstream
    VERSION ${VARN_LIBFFI_VERSION}
    GITHUB_REPOSITORY libffi/libffi
    GIT_TAG v${VARN_LIBFFI_VERSION}
    DOWNLOAD_ONLY YES
)

set(VARN_LIBFFI_ROOT "${varn_libffi_upstream_SOURCE_DIR}")
include("${CMAKE_CURRENT_LIST_DIR}/varn-libffi/varn-vendor-libffi.cmake")

if(NOT TARGET varn_ffi_vendor)
    set(_varn_ffi_gen_dir "${CMAKE_BINARY_DIR}/generated/varn_ffi")
    file(MAKE_DIRECTORY "${_varn_ffi_gen_dir}")
    set(LUA_FFI_VERSION_MAJOR 1)
    set(LUA_FFI_VERSION_MINOR 1)
    set(LUA_FFI_VERSION_PATCH 0)
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/src/varn/ffi/platform/lua_ffi_config.h.in"
        "${_varn_ffi_gen_dir}/config.h"
        @ONLY
    )

    add_library(varn_ffi_vendor STATIC
        "${CMAKE_CURRENT_SOURCE_DIR}/src/varn/ffi/vendor/ffi.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/varn/ffi/vendor/lex.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/varn/ffi/platform/varn_ffi_dl.c"
    )
    set_target_properties(varn_ffi_vendor PROPERTIES
        C_STANDARD 99
        C_STANDARD_REQUIRED ON
        POSITION_INDEPENDENT_CODE ON
    )
    if(MSVC)
        target_compile_options(varn_ffi_vendor PRIVATE /wd4244 /wd4267)
        target_include_directories(varn_ffi_vendor BEFORE PRIVATE
            "${_varn_ffi_gen_dir}"
            "${CMAKE_CURRENT_SOURCE_DIR}/src/varn/ffi/platform/msvc_compat"
        )
    else()
        target_compile_options(varn_ffi_vendor PRIVATE -fno-strict-aliasing -w)
        target_include_directories(varn_ffi_vendor BEFORE PRIVATE "${_varn_ffi_gen_dir}")
    endif()
    if(NOT MSVC)
        target_compile_definitions(varn_ffi_vendor PRIVATE _GNU_SOURCE)
    endif()
    target_include_directories(varn_ffi_vendor PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/src/varn/ffi/vendor"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/varn/ffi/platform"
    )
    target_include_directories(varn_ffi_vendor PRIVATE $<TARGET_PROPERTY:varn_vendor_lua,INTERFACE_INCLUDE_DIRECTORIES>)

    target_link_libraries(varn_ffi_vendor PUBLIC varn_vendor_libffi)
endif()
