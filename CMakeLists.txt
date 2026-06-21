cmake_minimum_required(VERSION 3.26)

# allow older cmake minimum version in dependencies
set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "")

project(varn VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

cmake_policy(SET CMP0054 NEW)
cmake_policy(SET CMP0076 NEW)
set(CMAKE_POLICY_DEFAULT_CMP0091 NEW)
set(CMAKE_POLICY_DEFAULT_CMP0135 NEW)

set(VARN_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
include("${VARN_ROOT}/cmake/helpers.cmake")

# generated version header
set(VARN_GENERATED_INCLUDE_DIR "${CMAKE_CURRENT_BINARY_DIR}/varn_generated")
configure_file(
    "${VARN_ROOT}/cmake/VarnVersion.h.in"
    "${VARN_GENERATED_INCLUDE_DIR}/VarnVersion.h"
    @ONLY
)

# toolchains set EMSCRIPTEN, and CMAKE_SYSTEM_NAME is a fallback so wasm builds still stub drivers.
if(EMSCRIPTEN OR CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
    set(VARN_BUILDING_FOR_EMSCRIPTEN ON)
else()
    set(VARN_BUILDING_FOR_EMSCRIPTEN OFF)
endif()

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release" "MinSizeRel" "RelWithDebInfo")
endif()

option(VARN_ENABLE_TLS "enable TLS (HTTPS) support" ON)
option(VARN_ENABLE_STATIC_FILES "enable public static file serving" ON)
option(VARN_WARNINGS_AS_ERRORS "treat warnings as errors" OFF)
option(VARN_ENABLE_ZIP "build the zip module (libzip + zlib)" ON)
option(VARN_BUILD_TESTS "build the native C++ test target (googletest)" OFF)
set(VARN_SANITIZE "" CACHE STRING "comma-separated sanitizers to build with, e.g. address,undefined (clang/gcc only)")

# the browser cannot host raw tcp or an in-page server, so force the wasm-capable driver set.
if(VARN_BUILDING_FOR_EMSCRIPTEN)
    set(VARN_ENABLE_TLS OFF CACHE BOOL "tls (disabled for Emscripten)" FORCE)
    set(VARN_HTTP_SERVER_DRIVER "DUMMY" CACHE STRING "" FORCE)
    set(VARN_HTTP_CLIENT_DRIVER "EMSCRIPTEN_FETCH" CACHE STRING "" FORCE)
    set(VARN_SOCKET_DRIVER "DUMMY" CACHE STRING "" FORCE)
    set(VARN_JSON_DRIVER "NLOHMANN" CACHE STRING "" FORCE)
    set(VARN_XML_DRIVER "PUGIXML" CACHE STRING "" FORCE)
    set(VARN_CRYPTO_DRIVER "PORTABLE" CACHE STRING "" FORCE)
    set(VARN_LOG_DRIVER "SPDLOG" CACHE STRING "" FORCE)
    set(VARN_FS_DRIVER "STD" CACHE STRING "" FORCE)
    set(VARN_FFI_DRIVER "DUMMY" CACHE STRING "" FORCE)
endif()

# apple platform deployment targets (matches dependency toolchains when cross-compiling)
if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    set(CMAKE_OSX_DEPLOYMENT_TARGET "15.0" CACHE STRING "iOS deployment target")
elseif(CMAKE_SYSTEM_NAME STREQUAL "watchOS")
    set(CMAKE_OSX_DEPLOYMENT_TARGET "8.0" CACHE STRING "watchOS deployment target")
elseif(CMAKE_SYSTEM_NAME STREQUAL "tvOS")
    set(CMAKE_OSX_DEPLOYMENT_TARGET "15.0" CACHE STRING "tvOS deployment target")
elseif(CMAKE_SYSTEM_NAME STREQUAL "visionOS")
    set(CMAKE_OSX_DEPLOYMENT_TARGET "1.0" CACHE STRING "visionOS deployment target")
endif()

# output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

# build accumulators populated by each module .cmake
set(VARN_SOURCES "")
set(VARN_INCLUDE_DIRS "")
set(VARN_COMPILE_DEFS "")

# tls always needs openssl, even when crypto uses it through poco.
if(VARN_ENABLE_TLS)
    set(VARN_NEEDS_OPENSSL ON)
endif()

# modules: core first, then the public api and features. each declares its sources, driver,
# and dependency needs.
foreach(_varn_module
    core api platform log async fs crypto json xml zip socket http ffi process datetime)
    include("${VARN_ROOT}/modules/${_varn_module}/${_varn_module}.cmake")
endforeach()

# dependencies driven by the VARN_NEEDS_* flags the modules set above
include("${VARN_ROOT}/cmake/cpm/cpm-downloader.cmake")
include("${VARN_ROOT}/cmake/dependencies.cmake")

if(NOT VARN_BUILDING_FOR_EMSCRIPTEN)
    find_package(Threads REQUIRED)
endif()

if(VARN_NEEDS_LIBFFI)
    include("${VARN_ROOT}/modules/ffi/libffi.cmake")
endif()

message(STATUS "Varn http server driver: ${VARN_HTTP_SERVER_DRIVER}")
message(STATUS "Varn http client driver: ${VARN_HTTP_CLIENT_DRIVER}")
message(STATUS "Varn socket driver: ${VARN_SOCKET_DRIVER}")
message(STATUS "Varn json driver: ${VARN_JSON_DRIVER}")
message(STATUS "Varn xml driver: ${VARN_XML_DRIVER}")
message(STATUS "Varn crypto driver: ${VARN_CRYPTO_DRIVER}")
message(STATUS "Varn log driver: ${VARN_LOG_DRIVER}")
message(STATUS "Varn fs driver: ${VARN_FS_DRIVER}")
message(STATUS "Varn ffi driver: ${VARN_FFI_DRIVER}")
message(STATUS "Varn tls enabled: ${VARN_ENABLE_TLS}")
message(STATUS "Varn zip module: ${VARN_ENABLE_ZIP}")

# build target selector. cli/wasm build an executable on top of the static core.
# apple/android build the embeddable shared library packaged downstream (xcframework / aar).
if(NOT DEFINED CACHE{VARN_TARGET})
    if(VARN_BUILDING_FOR_EMSCRIPTEN)
        set(VARN_TARGET "wasm" CACHE STRING "build target: cli wasm apple android")
    else()
        set(VARN_TARGET "cli" CACHE STRING "build target: cli wasm apple android")
    endif()
endif()
set_property(CACHE VARN_TARGET PROPERTY STRINGS cli wasm apple android)
set(_varn_target_ok cli wasm apple android)
if(NOT VARN_TARGET IN_LIST _varn_target_ok)
    message(FATAL_ERROR "VARN_TARGET must be one of: ${_varn_target_ok} (got '${VARN_TARGET}')")
endif()
message(STATUS "Varn build target: ${VARN_TARGET}")

if(VARN_TARGET STREQUAL "apple")
    # Varn.framework: a self-contained dynamic framework. compiling the sources into a shared
    # library pulls every module in, and the linker resolves the static deps it touches.
    add_library(Varn SHARED ${VARN_SOURCES} "${VARN_ROOT}/modules/api/include/varn/varn.h")
    varn_apply_usage(Varn PRIVATE)
    varn_apply_warnings(Varn)
    set_target_properties(Varn PROPERTIES
        OUTPUT_NAME "Varn"
        FRAMEWORK TRUE
        FRAMEWORK_VERSION A
        MACOSX_FRAMEWORK_IDENTIFIER "com.varn.runtime"
        MACOSX_FRAMEWORK_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_FRAMEWORK_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        PUBLIC_HEADER "${VARN_ROOT}/modules/api/include/varn/varn.h"
    )
elseif(VARN_TARGET STREQUAL "android")
    # static core only. the android/ gradle module links it with the jni bindings into libvarn.so.
    add_library(varn_core STATIC ${VARN_SOURCES})
    set_target_properties(varn_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
    varn_apply_usage(varn_core PUBLIC)
    varn_apply_warnings(varn_core)
else()
    # cli / wasm: build the static core once, then link the host executable against it.
    add_library(varn_core STATIC ${VARN_SOURCES})
    set_target_properties(varn_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
    varn_apply_usage(varn_core PUBLIC)
    varn_apply_warnings(varn_core)
    varn_apply_sanitize(varn_core PUBLIC)

    if(VARN_TARGET STREQUAL "wasm")
        add_executable(varn_wasm "${VARN_ROOT}/src/wasm/VarnWasm.cpp")
        target_link_libraries(varn_wasm PRIVATE varn_core)
        target_link_options(varn_wasm PRIVATE
            "SHELL:-s WASM=1"
            "SHELL:-s EXPORT_ES6=1"
            "SHELL:-s MODULARIZE=1"
            "SHELL:-s EXPORT_NAME=createVarnWasm"
            "SHELL:-s ALLOW_MEMORY_GROWTH=1"
            "SHELL:-s STACK_SIZE=8388608"
            "SHELL:-s INITIAL_MEMORY=67108864"
            "SHELL:-s ASYNCIFY"
            "SHELL:-s ASYNCIFY_STACK_SIZE=1048576"
            "SHELL:--bind"
            "SHELL:-s ENVIRONMENT=web,worker"
            "SHELL:-s FILESYSTEM=1"
        )
    elseif(NOT CMAKE_SYSTEM_NAME MATCHES "^(iOS|tvOS|watchOS|visionOS)$")
        # the host cli only makes sense on desktop. a mobile apple platform cannot run a plain
        # executable, so a cli build there stops at the static core.
        add_executable(varn "${VARN_ROOT}/src/main.cpp")
        target_link_libraries(varn PRIVATE varn_core)

        if(MSVC)
            # windows defaults the main thread to a 1 MB stack, well under the 8 MB unix gives. recursive
            # work such as parsing deeply nested json needs the same headroom, so match it here.
            target_link_options(varn PRIVATE /STACK:8388608)

            # the crash handler resolves a backtrace through dbghelp, and program-database symbols let it
            # print function names with file and line even in a release build.
            target_link_libraries(varn PRIVATE dbghelp)
            target_compile_options(varn_core PRIVATE /Zi)
            target_compile_options(varn PRIVATE /Zi)
            target_link_options(varn PRIVATE /DEBUG)
        endif()

        install(TARGETS varn RUNTIME DESTINATION bin)
    endif()
endif()

# native C++ unit tests, kept off the shippable build and out of the wasm/mobile targets.
if(VARN_BUILD_TESTS AND TARGET varn_core AND NOT VARN_BUILDING_FOR_EMSCRIPTEN)
    enable_testing()
    add_subdirectory("${VARN_ROOT}/tests/cpp")
endif()
