cmake_minimum_required(VERSION 3.22)

# allow older cmake minimum version in dependencies
set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "")

project(varn VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# policies
cmake_policy(SET CMP0054 NEW)
cmake_policy(SET CMP0076 NEW)
set(CMAKE_POLICY_DEFAULT_CMP0091 NEW)
set(CMAKE_POLICY_DEFAULT_CMP0135 NEW)

set(VARN_GENERATED_INCLUDE_DIR "${CMAKE_CURRENT_BINARY_DIR}/varn_generated")
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/VarnVersion.h.in"
    "${VARN_GENERATED_INCLUDE_DIR}/VarnVersion.h"
    @ONLY
)

# Toolchains should set EMSCRIPTEN; treat CMAKE_SYSTEM_NAME as a fallback so wasm builds still stub drivers.
if(EMSCRIPTEN OR CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
    set(VARN_BUILDING_FOR_EMSCRIPTEN ON)
else()
    set(VARN_BUILDING_FOR_EMSCRIPTEN OFF)
endif()

# build type
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release" "MinSizeRel" "RelWithDebInfo")
endif()

if(VARN_BUILDING_FOR_EMSCRIPTEN)
    # Browser: no raw TCP / in-page HTTP server; use Fetch for outbound HTTP, MEMFS for files, spdlog + nlohmann + pugixml.
    set(VARN_HTTP_SERVER_DRIVER "DUMMY" CACHE STRING "HTTP server transport (Emscripten: stub)" FORCE)
    set(VARN_HTTP_CLIENT_DRIVER "EMSCRIPTEN_FETCH" CACHE STRING "HTTP client (Emscripten: fetch)" FORCE)
    set(VARN_SOCKET_DRIVER "DUMMY" CACHE STRING "TCP socket transport (Emscripten: stub)" FORCE)
    set(VARN_JSON_DRIVER "NLOHMANN" CACHE STRING "json serializer (Emscripten: nlohmann/json)" FORCE)
    set(VARN_XML_DRIVER "PUGIXML" CACHE STRING "xml serializer (Emscripten: pugixml)" FORCE)
    set(VARN_CRYPTO_DRIVER "DUMMY" CACHE STRING "crypto primitives (Emscripten: dummy)" FORCE)
    set(VARN_LOG_DRIVER "SPDLOG" CACHE STRING "lua log backend (Emscripten: spdlog)" FORCE)
    set(VARN_FS_DRIVER "STD" CACHE STRING "fs storage (Emscripten: MEMFS via standard I/O)" FORCE)
    set(VARN_FFI_DRIVER "DUMMY" CACHE STRING "FFI (Emscripten: stub)" FORCE)
    set(VARN_ENABLE_TLS OFF CACHE BOOL "tls (disabled for Emscripten)" FORCE)
endif()

option(VARN_ENABLE_TLS "enable TLS (HTTPS) support" ON)
option(VARN_ENABLE_STATIC_FILES "enable public static file serving" ON)
option(VARN_WARNINGS_AS_ERRORS "treat warnings as errors" OFF)
option(VARN_ENABLE_ZIP "build Lua zip module (libzip + zlib)" ON)

set(VARN_LOG_DRIVER "SPDLOG" CACHE STRING "Log pipeline: STDOUT DUMMY SPDLOG")
set_property(CACHE VARN_LOG_DRIVER PROPERTY STRINGS STDOUT DUMMY SPDLOG)

set(VARN_FS_DRIVER "STD" CACHE STRING "fs module storage: STD DUMMY")
set_property(CACHE VARN_FS_DRIVER PROPERTY STRINGS STD DUMMY)

if(NOT VARN_BUILDING_FOR_EMSCRIPTEN)
    if(NOT DEFINED CACHE{VARN_FFI_DRIVER})
        set(VARN_FFI_DRIVER "LIBFFI" CACHE STRING "FFI: LIBFFI (vendored libffi via CMake + lua-ffi parser) or DUMMY")
    endif()
    set_property(CACHE VARN_FFI_DRIVER PROPERTY STRINGS LIBFFI DUMMY)
endif()

# One implementation per module; CMake picks exactly one driver source tree under each varn/<module>/drivers/<name>/
set(VARN_HTTP_SERVER_DRIVER "POCO" CACHE STRING "HTTP server transport: POCO DUMMY")
set_property(CACHE VARN_HTTP_SERVER_DRIVER PROPERTY STRINGS POCO DUMMY)

set(VARN_HTTP_CLIENT_DRIVER "POCO" CACHE STRING "HTTP client transport: POCO DUMMY EMSCRIPTEN_FETCH")
set_property(CACHE VARN_HTTP_CLIENT_DRIVER PROPERTY STRINGS POCO DUMMY EMSCRIPTEN_FETCH)

set(VARN_SOCKET_DRIVER "POCO" CACHE STRING "TCP socket transport: POCO DUMMY")
set_property(CACHE VARN_SOCKET_DRIVER PROPERTY STRINGS POCO DUMMY)

set(VARN_JSON_DRIVER "NLOHMANN" CACHE STRING "JSON serialization: DUMMY NLOHMANN (Poco is not a JSON driver)")
set_property(CACHE VARN_JSON_DRIVER PROPERTY STRINGS DUMMY NLOHMANN)

set(VARN_XML_DRIVER "PUGIXML" CACHE STRING "XML serialization: DUMMY PUGIXML")
set_property(CACHE VARN_XML_DRIVER PROPERTY STRINGS DUMMY PUGIXML)

if(NOT VARN_BUILDING_FOR_EMSCRIPTEN)
    if(VARN_ENABLE_TLS)
        set(VARN_CRYPTO_DRIVER "OPENSSL" CACHE STRING "Crypto primitives: OPENSSL DUMMY")
    else()
        set(VARN_CRYPTO_DRIVER "DUMMY" CACHE STRING "Crypto primitives: OPENSSL DUMMY")
    endif()
    set_property(CACHE VARN_CRYPTO_DRIVER PROPERTY STRINGS OPENSSL DUMMY)
endif()

string(TOUPPER "${VARN_HTTP_SERVER_DRIVER}" VARN_HTTP_SERVER_DRIVER)
set(VARN_HTTP_SERVER_DRIVER "${VARN_HTTP_SERVER_DRIVER}" CACHE STRING "" FORCE)
string(TOUPPER "${VARN_HTTP_CLIENT_DRIVER}" VARN_HTTP_CLIENT_DRIVER)
set(VARN_HTTP_CLIENT_DRIVER "${VARN_HTTP_CLIENT_DRIVER}" CACHE STRING "" FORCE)
string(TOUPPER "${VARN_SOCKET_DRIVER}" VARN_SOCKET_DRIVER)
set(VARN_SOCKET_DRIVER "${VARN_SOCKET_DRIVER}" CACHE STRING "" FORCE)
string(TOUPPER "${VARN_JSON_DRIVER}" VARN_JSON_DRIVER)
set(VARN_JSON_DRIVER "${VARN_JSON_DRIVER}" CACHE STRING "" FORCE)
string(TOUPPER "${VARN_XML_DRIVER}" VARN_XML_DRIVER)
set(VARN_XML_DRIVER "${VARN_XML_DRIVER}" CACHE STRING "" FORCE)
string(TOUPPER "${VARN_CRYPTO_DRIVER}" VARN_CRYPTO_DRIVER)
set(VARN_CRYPTO_DRIVER "${VARN_CRYPTO_DRIVER}" CACHE STRING "" FORCE)
string(TOUPPER "${VARN_LOG_DRIVER}" VARN_LOG_DRIVER)
set(VARN_LOG_DRIVER "${VARN_LOG_DRIVER}" CACHE STRING "" FORCE)
string(TOUPPER "${VARN_FS_DRIVER}" VARN_FS_DRIVER)
set(VARN_FS_DRIVER "${VARN_FS_DRIVER}" CACHE STRING "" FORCE)
string(TOUPPER "${VARN_FFI_DRIVER}" VARN_FFI_DRIVER)
set(VARN_FFI_DRIVER "${VARN_FFI_DRIVER}" CACHE STRING "" FORCE)

if(VARN_JSON_DRIVER STREQUAL "POCO")
    message(WARNING "VARN_JSON_DRIVER=POCO is removed (Poco is only used for HTTP/socket). Forcing NLOHMANN.")
    set(VARN_JSON_DRIVER "NLOHMANN" CACHE STRING "" FORCE)
endif()
# Drop a stale regular variable so the IN_LIST checks below read the updated CACHE entry (CMake keeps locals separate).
unset(VARN_JSON_DRIVER)

if(VARN_XML_DRIVER STREQUAL "NATIVE")
    message(WARNING "VARN_XML_DRIVER=NATIVE is removed; use PUGIXML. Forcing PUGIXML.")
    set(VARN_XML_DRIVER "PUGIXML" CACHE STRING "" FORCE)
endif()
unset(VARN_XML_DRIVER)

if(VARN_LOG_DRIVER STREQUAL "STDERR")
    message(WARNING "VARN_LOG_DRIVER=STDERR is deprecated; use STDOUT. Forcing STDOUT.")
    set(VARN_LOG_DRIVER "STDOUT" CACHE STRING "" FORCE)
endif()

# Legacy cache value NONE (old name) maps to DUMMY for stub/disabled driver ids.
foreach(_varn_legacy_var IN ITEMS VARN_HTTP_SERVER_DRIVER VARN_HTTP_CLIENT_DRIVER VARN_SOCKET_DRIVER VARN_JSON_DRIVER VARN_XML_DRIVER VARN_CRYPTO_DRIVER VARN_LOG_DRIVER VARN_FS_DRIVER VARN_FFI_DRIVER)
    if("${${_varn_legacy_var}}" STREQUAL "NONE")
        message(WARNING "${_varn_legacy_var}=NONE is deprecated; use DUMMY. Forcing DUMMY.")
        set(${_varn_legacy_var} "DUMMY" CACHE STRING "" FORCE)
    endif()
endforeach()

set(_varn_http_server_ok "POCO;DUMMY")
set(_varn_http_client_ok "POCO;DUMMY;EMSCRIPTEN_FETCH")
set(_varn_socket_ok "POCO;DUMMY")
set(_varn_json_ok "DUMMY;NLOHMANN")
set(_varn_xml_ok "DUMMY;PUGIXML")
set(_varn_crypto_ok "OPENSSL;DUMMY")
set(_varn_log_ok "STDOUT;DUMMY;SPDLOG")
set(_varn_fs_ok "STD;DUMMY")
set(_varn_ffi_ok "LIBFFI;DUMMY")
if(NOT VARN_HTTP_SERVER_DRIVER IN_LIST _varn_http_server_ok)
    message(FATAL_ERROR "VARN_HTTP_SERVER_DRIVER must be one of: ${_varn_http_server_ok} (got '${VARN_HTTP_SERVER_DRIVER}')")
endif()
if(NOT VARN_HTTP_CLIENT_DRIVER IN_LIST _varn_http_client_ok)
    message(FATAL_ERROR "VARN_HTTP_CLIENT_DRIVER must be one of: ${_varn_http_client_ok} (got '${VARN_HTTP_CLIENT_DRIVER}')")
endif()
if(NOT VARN_SOCKET_DRIVER IN_LIST _varn_socket_ok)
    message(FATAL_ERROR "VARN_SOCKET_DRIVER must be one of: ${_varn_socket_ok} (got '${VARN_SOCKET_DRIVER}')")
endif()
if(NOT VARN_JSON_DRIVER IN_LIST _varn_json_ok)
    message(FATAL_ERROR "VARN_JSON_DRIVER must be one of: ${_varn_json_ok} (got '${VARN_JSON_DRIVER}')")
endif()
if(NOT VARN_XML_DRIVER IN_LIST _varn_xml_ok)
    message(FATAL_ERROR "VARN_XML_DRIVER must be one of: ${_varn_xml_ok} (got '${VARN_XML_DRIVER}')")
endif()
if(NOT VARN_CRYPTO_DRIVER IN_LIST _varn_crypto_ok)
    message(FATAL_ERROR "VARN_CRYPTO_DRIVER must be one of: ${_varn_crypto_ok} (got '${VARN_CRYPTO_DRIVER}')")
endif()
if(NOT VARN_LOG_DRIVER IN_LIST _varn_log_ok)
    message(FATAL_ERROR "VARN_LOG_DRIVER must be one of: ${_varn_log_ok} (got '${VARN_LOG_DRIVER}')")
endif()
if(NOT VARN_FS_DRIVER IN_LIST _varn_fs_ok)
    message(FATAL_ERROR "VARN_FS_DRIVER must be one of: ${_varn_fs_ok} (got '${VARN_FS_DRIVER}')")
endif()
if(NOT VARN_FFI_DRIVER IN_LIST _varn_ffi_ok)
    message(FATAL_ERROR "VARN_FFI_DRIVER must be one of: ${_varn_ffi_ok} (got '${VARN_FFI_DRIVER}')")
endif()

if(VARN_ENABLE_TLS AND NOT VARN_CRYPTO_DRIVER STREQUAL "OPENSSL")
    message(FATAL_ERROR "VARN_ENABLE_TLS=ON requires VARN_CRYPTO_DRIVER=OPENSSL.")
endif()

if(VARN_HTTP_SERVER_DRIVER STREQUAL "POCO")
    if(NOT VARN_JSON_DRIVER STREQUAL "NLOHMANN" AND NOT VARN_JSON_DRIVER STREQUAL "DUMMY")
        message(FATAL_ERROR "VARN_HTTP_SERVER_DRIVER=POCO requires VARN_JSON_DRIVER=NLOHMANN or DUMMY (Lua res:json when NLOHMANN).")
    endif()
endif()

if(VARN_HTTP_CLIENT_DRIVER STREQUAL "EMSCRIPTEN_FETCH" AND NOT VARN_BUILDING_FOR_EMSCRIPTEN)
    message(FATAL_ERROR "VARN_HTTP_CLIENT_DRIVER=EMSCRIPTEN_FETCH is only valid for Emscripten/WebAssembly builds.")
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
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin")

set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/lib")

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/lib")

message(STATUS "Varn HTTP server driver: ${VARN_HTTP_SERVER_DRIVER}")
message(STATUS "Varn HTTP client driver: ${VARN_HTTP_CLIENT_DRIVER}")
message(STATUS "Varn socket driver: ${VARN_SOCKET_DRIVER}")
message(STATUS "Varn JSON driver: ${VARN_JSON_DRIVER}")
message(STATUS "Varn XML driver: ${VARN_XML_DRIVER}")
message(STATUS "Varn crypto driver: ${VARN_CRYPTO_DRIVER}")
message(STATUS "Varn log driver: ${VARN_LOG_DRIVER}")
message(STATUS "Varn fs driver: ${VARN_FS_DRIVER}")
message(STATUS "Varn FFI driver: ${VARN_FFI_DRIVER}")
message(STATUS "Varn TLS enabled: ${VARN_ENABLE_TLS}")
message(STATUS "Varn zip module (libzip): ${VARN_ENABLE_ZIP}")

# dependencies (cpm: lua, optional openssl, optional poco)
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/cpm/cpm-downloader.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/cpm/varn-dependencies.cmake")

if(NOT VARN_BUILDING_FOR_EMSCRIPTEN)
    find_package(Threads REQUIRED)
endif()

if(VARN_FFI_DRIVER STREQUAL "LIBFFI")
    include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/varn-libffi.cmake")
endif()

set(VARN_SOURCES
    src/varn/platform/PlatformInfo.cpp
    src/varn/platform/PlatformModule.cpp
    src/varn/zip/ZipModule.cpp
    src/varn/runtime/App.cpp
    src/varn/runtime/Runtime.cpp
    src/varn/runtime/EventLoop.cpp
    src/varn/runtime/TaskPool.cpp
    src/varn/lua/LuaEngine.cpp
    src/varn/lua/LuaHelpers.cpp
    src/varn/lua/NativeModules.cpp
    src/varn/async/Promise.cpp
    src/varn/async/AsyncModule.cpp
    src/varn/fs/FsModule.cpp
    src/varn/log/LogModule.cpp
    src/varn/log/Log.cpp
    src/varn/crypto/CryptoModule.cpp
    src/varn/http/HttpTypes.cpp
    src/varn/http/HttpServerFactory.cpp
)

if(VARN_CRYPTO_DRIVER STREQUAL "OPENSSL")
    list(APPEND VARN_SOURCES src/varn/crypto/drivers/openssl/OpenSslCryptoPrimitives.cpp)
elseif(VARN_CRYPTO_DRIVER STREQUAL "DUMMY")
    list(APPEND VARN_SOURCES src/varn/crypto/drivers/dummy/DummyCryptoPrimitives.cpp)
else()
    message(FATAL_ERROR "Unhandled VARN_CRYPTO_DRIVER='${VARN_CRYPTO_DRIVER}' (expected OPENSSL or DUMMY).")
endif()

if(VARN_FS_DRIVER STREQUAL "STD")
    list(APPEND VARN_SOURCES src/varn/fs/drivers/std/StdFsStorage.cpp)
elseif(VARN_FS_DRIVER STREQUAL "DUMMY")
    list(APPEND VARN_SOURCES src/varn/fs/drivers/dummy/DummyFsStorage.cpp)
else()
    message(FATAL_ERROR "Unhandled VARN_FS_DRIVER='${VARN_FS_DRIVER}' (expected STD or DUMMY).")
endif()

if(VARN_LOG_DRIVER STREQUAL "STDOUT")
    list(APPEND VARN_SOURCES src/varn/log/drivers/stdout/StdoutLogBackend.cpp)
elseif(VARN_LOG_DRIVER STREQUAL "SPDLOG")
    list(APPEND VARN_SOURCES src/varn/log/drivers/spdlog/SpdlogLogBackend.cpp)
elseif(VARN_LOG_DRIVER STREQUAL "DUMMY")
    list(APPEND VARN_SOURCES src/varn/log/drivers/dummy/DummyLogBackend.cpp)
else()
    message(FATAL_ERROR "Unhandled VARN_LOG_DRIVER='${VARN_LOG_DRIVER}' (expected STDOUT, SPDLOG, or DUMMY).")
endif()

if(VARN_HTTP_SERVER_DRIVER STREQUAL "POCO")
    list(APPEND VARN_SOURCES
        src/varn/http/HttpServerModule.cpp
        src/varn/http/StaticFileHandler.cpp
        src/varn/http/MimeTypes.cpp
        src/varn/http/drivers/poco/PocoHttpServer.cpp
    )
else()
    list(APPEND VARN_SOURCES src/varn/http/drivers/dummy/HttpServerModuleStub.cpp)
endif()

list(APPEND VARN_SOURCES src/varn/http/HttpClientModule.cpp)

if(VARN_HTTP_CLIENT_DRIVER STREQUAL "POCO")
    list(APPEND VARN_SOURCES src/varn/http/drivers/poco/PocoHttpClient.cpp)
elseif(VARN_HTTP_CLIENT_DRIVER STREQUAL "EMSCRIPTEN_FETCH")
    list(APPEND VARN_SOURCES src/varn/http/drivers/emscripten_fetch/EmscriptenFetchHttpClient.cpp)
else()
    list(APPEND VARN_SOURCES src/varn/http/drivers/dummy/DummyHttpClient.cpp)
endif()

list(APPEND VARN_SOURCES src/varn/socket/SocketModule.cpp)

if(VARN_SOCKET_DRIVER STREQUAL "POCO")
    list(APPEND VARN_SOURCES src/varn/socket/drivers/poco/PocoSocketTcp.cpp)
else()
    list(APPEND VARN_SOURCES src/varn/socket/drivers/dummy/DummySocketModule.cpp)
endif()

if(VARN_JSON_DRIVER STREQUAL "NLOHMANN")
    list(APPEND VARN_SOURCES src/varn/json/drivers/nlohmann/NlohmannJsonSerializer.cpp)
else()
    list(APPEND VARN_SOURCES src/varn/json/drivers/dummy/DummyJsonSerializer.cpp)
endif()

if(VARN_XML_DRIVER STREQUAL "PUGIXML")
    list(APPEND VARN_SOURCES src/varn/xml/drivers/pugixml/PugixmlXmlSerializer.cpp)
else()
    list(APPEND VARN_SOURCES src/varn/xml/drivers/dummy/DummyXmlSerializer.cpp)
endif()

list(APPEND VARN_SOURCES src/varn/ffi/FfiModule.cpp)
if(VARN_FFI_DRIVER STREQUAL "LIBFFI")
elseif(VARN_FFI_DRIVER STREQUAL "DUMMY")
    list(APPEND VARN_SOURCES src/varn/ffi/drivers/dummy/DummyFfiModule.cpp)
else()
    message(FATAL_ERROR "Unhandled VARN_FFI_DRIVER='${VARN_FFI_DRIVER}' (expected LIBFFI or DUMMY).")
endif()

if(VARN_BUILDING_FOR_EMSCRIPTEN)
    add_executable(varn_wasm src/varn/wasm/VarnWasm.cpp ${VARN_SOURCES})
    target_include_directories(varn_wasm PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${VARN_GENERATED_INCLUDE_DIR}"
    )
    target_link_libraries(varn_wasm PRIVATE
        varn_vendor_lua
        spdlog::spdlog_header_only
        nlohmann_json::nlohmann_json
        pugixml::pugixml
    )
    if(VARN_ENABLE_ZIP)
        target_compile_definitions(varn_wasm PRIVATE VARN_HAVE_LIBZIP=1)
        if(TARGET libzip::zip)
            target_link_libraries(varn_wasm PRIVATE libzip::zip ZLIB::ZLIB)
        elseif(TARGET zip)
            target_link_libraries(varn_wasm PRIVATE zip ZLIB::ZLIB)
        else()
            message(FATAL_ERROR "libzip target not found after CPM (expected libzip::zip or zip)")
        endif()
    endif()
    string(TOUPPER "${VARN_HTTP_SERVER_DRIVER}" VARN_HTTP_SERVER_DRIVER_U)
    string(TOUPPER "${VARN_HTTP_CLIENT_DRIVER}" VARN_HTTP_CLIENT_DRIVER_U)
    string(TOUPPER "${VARN_SOCKET_DRIVER}" VARN_SOCKET_DRIVER_U)
    string(TOUPPER "${VARN_JSON_DRIVER}" VARN_JSON_DRIVER_U)
    string(TOUPPER "${VARN_XML_DRIVER}" VARN_XML_DRIVER_U)
    string(TOUPPER "${VARN_CRYPTO_DRIVER}" VARN_CRYPTO_DRIVER_U)
    string(TOUPPER "${VARN_LOG_DRIVER}" VARN_LOG_DRIVER_U)
    string(TOUPPER "${VARN_FS_DRIVER}" VARN_FS_DRIVER_U)
    string(TOUPPER "${VARN_FFI_DRIVER}" VARN_FFI_DRIVER_U)
    target_compile_definitions(varn_wasm PRIVATE
        VARN_HTTP_SERVER_DRIVER_${VARN_HTTP_SERVER_DRIVER_U}=1
        "VARN_HTTP_SERVER_DRIVER_NAME=\"${VARN_HTTP_SERVER_DRIVER}\""
        VARN_HTTP_CLIENT_DRIVER_${VARN_HTTP_CLIENT_DRIVER_U}=1
        "VARN_HTTP_CLIENT_DRIVER_NAME=\"${VARN_HTTP_CLIENT_DRIVER}\""
        VARN_SOCKET_DRIVER_${VARN_SOCKET_DRIVER_U}=1
        "VARN_SOCKET_DRIVER_NAME=\"${VARN_SOCKET_DRIVER}\""
        VARN_JSON_DRIVER_${VARN_JSON_DRIVER_U}=1
        "VARN_JSON_DRIVER_NAME=\"${VARN_JSON_DRIVER}\""
        VARN_XML_DRIVER_${VARN_XML_DRIVER_U}=1
        "VARN_XML_DRIVER_NAME=\"${VARN_XML_DRIVER}\""
        VARN_CRYPTO_DRIVER_${VARN_CRYPTO_DRIVER_U}=1
        "VARN_CRYPTO_DRIVER_NAME=\"${VARN_CRYPTO_DRIVER}\""
        VARN_LOG_DRIVER_${VARN_LOG_DRIVER_U}=1
        "VARN_LOG_DRIVER_NAME=\"${VARN_LOG_DRIVER}\""
        VARN_FS_DRIVER_${VARN_FS_DRIVER_U}=1
        "VARN_FS_DRIVER_NAME=\"${VARN_FS_DRIVER}\""
        VARN_FFI_DRIVER_${VARN_FFI_DRIVER_U}=1
        "VARN_FFI_DRIVER_NAME=\"${VARN_FFI_DRIVER}\""
    )
    set_target_properties(varn_wasm PROPERTIES OUTPUT_NAME "varn_wasm")
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
    if(NOT MSVC)
        target_compile_options(varn_wasm PRIVATE -Wall -Wextra -Wpedantic)
        if(VARN_WARNINGS_AS_ERRORS)
            target_compile_options(varn_wasm PRIVATE -Werror)
        endif()
    endif()
else()
    add_executable(varn
        src/main.cpp
        ${VARN_SOURCES}
    )

    target_include_directories(varn PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${VARN_GENERATED_INCLUDE_DIR}"
    )

    target_link_libraries(varn PRIVATE
        varn_vendor_lua
        Threads::Threads
    )
    if(VARN_LOG_DRIVER STREQUAL "SPDLOG")
        target_link_libraries(varn PRIVATE spdlog::spdlog_header_only)
    endif()
    if(VARN_JSON_DRIVER STREQUAL "NLOHMANN")
        target_link_libraries(varn PRIVATE nlohmann_json::nlohmann_json)
    endif()
    if(VARN_XML_DRIVER STREQUAL "PUGIXML")
        target_link_libraries(varn PRIVATE pugixml::pugixml)
    endif()
    if(VARN_ENABLE_ZIP)
        target_compile_definitions(varn PRIVATE VARN_HAVE_LIBZIP=1)
        if(TARGET libzip::zip)
            target_link_libraries(varn PRIVATE libzip::zip ZLIB::ZLIB)
        elseif(TARGET zip)
            target_link_libraries(varn PRIVATE zip ZLIB::ZLIB)
        else()
            message(FATAL_ERROR "libzip target not found after CPM (expected libzip::zip or zip)")
        endif()
    endif()

    string(TOUPPER "${VARN_HTTP_SERVER_DRIVER}" VARN_HTTP_SERVER_DRIVER_U)
    string(TOUPPER "${VARN_HTTP_CLIENT_DRIVER}" VARN_HTTP_CLIENT_DRIVER_U)
    string(TOUPPER "${VARN_SOCKET_DRIVER}" VARN_SOCKET_DRIVER_U)
    string(TOUPPER "${VARN_JSON_DRIVER}" VARN_JSON_DRIVER_U)
    string(TOUPPER "${VARN_XML_DRIVER}" VARN_XML_DRIVER_U)
    string(TOUPPER "${VARN_CRYPTO_DRIVER}" VARN_CRYPTO_DRIVER_U)
    string(TOUPPER "${VARN_LOG_DRIVER}" VARN_LOG_DRIVER_U)
    string(TOUPPER "${VARN_FS_DRIVER}" VARN_FS_DRIVER_U)
    string(TOUPPER "${VARN_FFI_DRIVER}" VARN_FFI_DRIVER_U)
    target_compile_definitions(varn PRIVATE
        VARN_HTTP_SERVER_DRIVER_${VARN_HTTP_SERVER_DRIVER_U}=1
        "VARN_HTTP_SERVER_DRIVER_NAME=\"${VARN_HTTP_SERVER_DRIVER}\""
        VARN_HTTP_CLIENT_DRIVER_${VARN_HTTP_CLIENT_DRIVER_U}=1
        "VARN_HTTP_CLIENT_DRIVER_NAME=\"${VARN_HTTP_CLIENT_DRIVER}\""
        VARN_SOCKET_DRIVER_${VARN_SOCKET_DRIVER_U}=1
        "VARN_SOCKET_DRIVER_NAME=\"${VARN_SOCKET_DRIVER}\""
        VARN_JSON_DRIVER_${VARN_JSON_DRIVER_U}=1
        "VARN_JSON_DRIVER_NAME=\"${VARN_JSON_DRIVER}\""
        VARN_XML_DRIVER_${VARN_XML_DRIVER_U}=1
        "VARN_XML_DRIVER_NAME=\"${VARN_XML_DRIVER}\""
        VARN_CRYPTO_DRIVER_${VARN_CRYPTO_DRIVER_U}=1
        "VARN_CRYPTO_DRIVER_NAME=\"${VARN_CRYPTO_DRIVER}\""
        VARN_LOG_DRIVER_${VARN_LOG_DRIVER_U}=1
        "VARN_LOG_DRIVER_NAME=\"${VARN_LOG_DRIVER}\""
        VARN_FS_DRIVER_${VARN_FS_DRIVER_U}=1
        "VARN_FS_DRIVER_NAME=\"${VARN_FS_DRIVER}\""
        VARN_FFI_DRIVER_${VARN_FFI_DRIVER_U}=1
        "VARN_FFI_DRIVER_NAME=\"${VARN_FFI_DRIVER}\""
    )

    if(VARN_ENABLE_STATIC_FILES)
        target_compile_definitions(varn PRIVATE VARN_ENABLE_STATIC_FILES=1)
    endif()

    if(VARN_ENABLE_TLS)
        target_compile_definitions(varn PRIVATE VARN_ENABLE_TLS=1)
    endif()

    set(_varn_openssl_from_poco FALSE)
    if(VARN_ENABLE_TLS
        AND (VARN_HTTP_SERVER_DRIVER STREQUAL "POCO"
            OR VARN_HTTP_CLIENT_DRIVER STREQUAL "POCO"
            OR VARN_SOCKET_DRIVER STREQUAL "POCO"))
        set(_varn_openssl_from_poco TRUE)
    endif()

    if(VARN_ENABLE_TLS)
        if(NOT _varn_openssl_from_poco)
            target_link_libraries(varn PRIVATE OpenSSL::SSL OpenSSL::Crypto)
        endif()
    elseif(VARN_CRYPTO_DRIVER STREQUAL "OPENSSL")
        target_link_libraries(varn PRIVATE OpenSSL::Crypto)
    endif()

    set(_varn_poco_libs "")
    if(VARN_HTTP_SERVER_DRIVER STREQUAL "POCO"
        OR VARN_HTTP_CLIENT_DRIVER STREQUAL "POCO"
        OR VARN_SOCKET_DRIVER STREQUAL "POCO")
        if(VARN_ENABLE_TLS)
            if(WIN32)
                list(APPEND _varn_poco_libs Poco::NetSSLWin Poco::Crypto)
            else()
                list(APPEND _varn_poco_libs Poco::NetSSL)
            endif()
        else()
            list(APPEND _varn_poco_libs Poco::Net Poco::Util)
        endif()
    endif()
    if(_varn_poco_libs)
        target_link_libraries(varn PRIVATE ${_varn_poco_libs})
    endif()

    if(VARN_FFI_DRIVER STREQUAL "LIBFFI")
        target_link_libraries(varn PRIVATE varn_ffi_vendor)
    endif()

    if(MSVC)
        target_compile_options(varn PRIVATE /W4)
        if(VARN_WARNINGS_AS_ERRORS)
            target_compile_options(varn PRIVATE /WX)
        endif()
    else()
        target_compile_options(varn PRIVATE -Wall -Wextra -Wpedantic)
        if(VARN_WARNINGS_AS_ERRORS)
            target_compile_options(varn PRIVATE -Werror)
        endif()
    endif()

    if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
        install(TARGETS varn BUNDLE DESTINATION bin)
    else()
        install(TARGETS varn RUNTIME DESTINATION bin)
    endif()
endif()
