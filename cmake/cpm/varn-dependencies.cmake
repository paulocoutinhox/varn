# Lua
CPMAddPackage(
    NAME lua
    VERSION 5.5.0
    GITHUB_REPOSITORY lua/lua
    GIT_TAG v5.5.0
    DOWNLOAD_ONLY YES
)

if(NOT TARGET varn_vendor_lua)
    set(_varn_lua_sources
        lapi.c
        lauxlib.c
        lbaselib.c
        lcode.c
        lcorolib.c
        lctype.c
        ldblib.c
        ldebug.c
        ldo.c
        ldump.c
        lfunc.c
        lgc.c
        linit.c
        liolib.c
        llex.c
        lmathlib.c
        lmem.c
        loadlib.c
        lobject.c
        lopcodes.c
        loslib.c
        lparser.c
        lstate.c
        lstring.c
        lstrlib.c
        ltable.c
        ltablib.c
        ltm.c
        lundump.c
        lutf8lib.c
        lvm.c
        lzio.c
    )
    list(TRANSFORM _varn_lua_sources PREPEND "${lua_SOURCE_DIR}/")

    add_library(varn_vendor_lua STATIC ${_varn_lua_sources})
    target_include_directories(varn_vendor_lua PUBLIC "${lua_SOURCE_DIR}")
    set_target_properties(varn_vendor_lua PROPERTIES
        C_STANDARD 99
        C_STANDARD_REQUIRED ON
        POSITION_INDEPENDENT_CODE ON
    )

    if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
        target_compile_definitions(varn_vendor_lua PRIVATE LUA_USE_MACOSX)
    elseif(CMAKE_SYSTEM_NAME STREQUAL "iOS")
        target_compile_definitions(varn_vendor_lua PRIVATE LUA_USE_IOS)
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        target_compile_definitions(varn_vendor_lua PRIVATE LUA_USE_LINUX)
        target_link_libraries(varn_vendor_lua PUBLIC dl m)
    elseif(ANDROID)
        target_compile_definitions(varn_vendor_lua PRIVATE LUA_USE_LINUX)
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
        target_compile_definitions(varn_vendor_lua PRIVATE LUA_USE_LINUX)
    elseif(WIN32)
        # luaconf.h enables lua_use_windows when _win32 is defined
    else()
        message(FATAL_ERROR "varn bundled lua: unsupported platform (${CMAKE_SYSTEM_NAME})")
    endif()
endif()

# OpenSSL
if((VARN_ENABLE_TLS OR VARN_CRYPTO_DRIVER STREQUAL "OPENSSL") AND NOT CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
    if(CMAKE_OSX_ARCHITECTURES)
        list(GET CMAKE_OSX_ARCHITECTURES 0 _ossl_arch)
    else()
        set(_ossl_arch "${CMAKE_SYSTEM_PROCESSOR}")
    endif()

    set(_ossl_target_platform "")

    if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
        if(CMAKE_OSX_SYSROOT MATCHES "[Ss]imulator")
            if(_ossl_arch STREQUAL "x86_64")
                set(_ossl_target_platform "iossimulator-x86_64-xcrun")
            else()
                set(_ossl_target_platform "iossimulator-arm64-xcrun")
            endif()
        else()
            set(_ossl_target_platform "ios64-xcrun")
        endif()
    elseif(ANDROID)
        if(CMAKE_ANDROID_ARCH_ABI STREQUAL "arm64-v8a")
            set(_ossl_target_platform "android-arm64")
        elseif(CMAKE_ANDROID_ARCH_ABI STREQUAL "armeabi-v7a")
            set(_ossl_target_platform "android-arm")
        elseif(CMAKE_ANDROID_ARCH_ABI STREQUAL "x86_64")
            set(_ossl_target_platform "android-x86_64")
        elseif(CMAKE_ANDROID_ARCH_ABI STREQUAL "x86")
            set(_ossl_target_platform "android-x86")
        endif()
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
        if(_ossl_arch MATCHES "arm64|aarch64")
            set(_ossl_target_platform "darwin64-arm64-cc")
        else()
            set(_ossl_target_platform "darwin64-x86_64-cc")
        endif()
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        if(_ossl_arch MATCHES "arm64|aarch64")
            set(_ossl_target_platform "linux-aarch64")
        else()
            set(_ossl_target_platform "linux-x86_64")
        endif()
    elseif(WIN32)
        if(_ossl_arch MATCHES "ARM64|aarch64|arm64")
            set(_ossl_target_platform "VC-WIN64-ARM")
        elseif(CMAKE_SIZEOF_VOID_P EQUAL 8)
            set(_ossl_target_platform "VC-WIN64A")
        else()
            set(_ossl_target_platform "VC-WIN32")
        endif()
    endif()

    set(_ossl_cpm_options "OPENSSL_TARGET_VERSION 3.6.1")
    if(_ossl_target_platform)
        list(APPEND _ossl_cpm_options "OPENSSL_TARGET_PLATFORM ${_ossl_target_platform}")
    endif()

    if(CMAKE_SYSTEM_NAME STREQUAL "iOS" AND CMAKE_OSX_DEPLOYMENT_TARGET)
        if(CMAKE_OSX_SYSROOT MATCHES "[Ss]imulator")
            list(APPEND _ossl_cpm_options
                "OPENSSL_CONFIGURE_OPTIONS -mios-simulator-version-min=${CMAKE_OSX_DEPLOYMENT_TARGET}")
        else()
            list(APPEND _ossl_cpm_options
                "OPENSSL_CONFIGURE_OPTIONS -miphoneos-version-min=${CMAKE_OSX_DEPLOYMENT_TARGET}")
        endif()
    elseif(CMAKE_SYSTEM_NAME STREQUAL "tvOS" AND CMAKE_OSX_DEPLOYMENT_TARGET)
        if(CMAKE_OSX_SYSROOT MATCHES "[Ss]imulator")
            list(APPEND _ossl_cpm_options
                "OPENSSL_CONFIGURE_OPTIONS -mtvos-simulator-version-min=${CMAKE_OSX_DEPLOYMENT_TARGET}")
        else()
            list(APPEND _ossl_cpm_options
                "OPENSSL_CONFIGURE_OPTIONS -mtvos-version-min=${CMAKE_OSX_DEPLOYMENT_TARGET}")
        endif()
    elseif(CMAKE_SYSTEM_NAME STREQUAL "watchOS" AND CMAKE_OSX_DEPLOYMENT_TARGET)
        if(CMAKE_OSX_SYSROOT MATCHES "[Ss]imulator")
            list(APPEND _ossl_cpm_options
                "OPENSSL_CONFIGURE_OPTIONS -mwatchos-simulator-version-min=${CMAKE_OSX_DEPLOYMENT_TARGET}")
        else()
            list(APPEND _ossl_cpm_options
                "OPENSSL_CONFIGURE_OPTIONS -mwatchos-version-min=${CMAKE_OSX_DEPLOYMENT_TARGET}")
        endif()
    elseif(CMAKE_SYSTEM_NAME STREQUAL "visionOS" AND CMAKE_OSX_DEPLOYMENT_TARGET)
        if(CMAKE_OSX_SYSROOT MATCHES "[Ss]imulator")
            list(APPEND _ossl_cpm_options
                "OPENSSL_CONFIGURE_OPTIONS -mtargetos=xros${CMAKE_OSX_DEPLOYMENT_TARGET}-simulator")
        else()
            list(APPEND _ossl_cpm_options
                "OPENSSL_CONFIGURE_OPTIONS -mtargetos=xros${CMAKE_OSX_DEPLOYMENT_TARGET}")
        endif()
    elseif(DEFINED CMAKE_OSX_DEPLOYMENT_TARGET AND CMAKE_OSX_DEPLOYMENT_TARGET AND CMAKE_SYSTEM_NAME STREQUAL "Darwin")
        list(APPEND _ossl_cpm_options
            "OPENSSL_CONFIGURE_OPTIONS -mmacosx-version-min=${CMAKE_OSX_DEPLOYMENT_TARGET}")
    endif()

    if(ANDROID)
        set(ENV{ANDROID_API} ${ANDROID_NATIVE_API_LEVEL})
        set(ENV{ANDROID_NDK_ROOT} ${CMAKE_ANDROID_NDK})
        list(APPEND _ossl_cpm_options "OPENSSL_CONFIGURE_OPTIONS no-ui-console\\;no-engine")
    endif()

    CPMAddPackage(
        NAME openssl-cmake
        URL https://github.com/jimmy-park/openssl-cmake/archive/main.tar.gz
        OPTIONS ${_ossl_cpm_options}
    )
endif()

# Poco
set(_varn_poco_off_components
    "ENABLE_XML OFF"
    "ENABLE_MONGODB OFF"
    "ENABLE_DATA OFF"
    "ENABLE_DATA_SQLITE OFF"
    "ENABLE_DATA_MYSQL OFF"
    "ENABLE_DATA_POSTGRESQL OFF"
    "ENABLE_DATA_ODBC OFF"
    "POCO_ENABLE_SQL OFF"
    "ENABLE_REDIS OFF"
    "ENABLE_PROMETHEUS OFF"
    "ENABLE_ENCODINGS OFF"
    "ENABLE_ENCODINGS_COMPILER OFF"
    "ENABLE_PAGECOMPILER OFF"
    "ENABLE_PAGECOMPILER_FILE2PAGE OFF"
    "ENABLE_ACTIVERECORD OFF"
    "ENABLE_ACTIVERECORD_COMPILER OFF"
    "ENABLE_ZIP OFF"
    "ENABLE_JWT OFF"
    "ENABLE_APACHECONNECTOR OFF"
    "ENABLE_TESTS OFF"
    "ENABLE_SAMPLES OFF"
)

if(VARN_HTTP_SERVER_DRIVER STREQUAL "POCO" OR VARN_HTTP_CLIENT_DRIVER STREQUAL "POCO" OR VARN_SOCKET_DRIVER STREQUAL "POCO")
    if(VARN_ENABLE_TLS)
        if(WIN32)
            set(_varn_poco_netssl_options
                "ENABLE_NETSSL OFF"
                "ENABLE_NETSSL_WIN ON"
            )
        else()
            set(_varn_poco_netssl_options
                "ENABLE_NETSSL ON"
                "ENABLE_NETSSL_WIN OFF"
            )
        endif()
        set(_varn_poco_crypto_option "ENABLE_CRYPTO ON")
    else()
        set(_varn_poco_netssl_options
            "ENABLE_NETSSL OFF"
            "ENABLE_NETSSL_WIN OFF"
        )
        set(_varn_poco_crypto_option "ENABLE_CRYPTO OFF")
    endif()

    CPMAddPackage(
        NAME Poco
        VERSION 1.15.2
        GITHUB_REPOSITORY pocoproject/poco
        GIT_TAG poco-1.15.2-release
        OPTIONS
            "BUILD_SHARED_LIBS OFF"
            "ENABLE_FOUNDATION ON"
            "ENABLE_NET ON"
            ${_varn_poco_netssl_options}
            ${_varn_poco_crypto_option}
            "ENABLE_UTIL ON"
            "ENABLE_JSON OFF"
            ${_varn_poco_off_components}
    )
endif()

# spdlog / nlohmann_json / pugixml — used when LOG/JSON/XML drivers select them (all platforms, including Emscripten).
CPMAddPackage(
    NAME spdlog
    VERSION 1.17.0
    GITHUB_REPOSITORY gabime/spdlog
    GIT_TAG v1.17.0
    OPTIONS
        "SPDLOG_BUILD_EXAMPLE OFF"
        "SPDLOG_BUILD_TESTS OFF"
)

CPMAddPackage(
    NAME nlohmann_json
    VERSION 3.11.3
    GITHUB_REPOSITORY nlohmann/json
    GIT_TAG v3.11.3
)
CPMAddPackage(
    NAME pugixml
    VERSION 1.14
    GITHUB_REPOSITORY zeux/pugixml
    GIT_TAG v1.14
    OPTIONS
        "BUILD_SHARED_LIBS OFF"
)

if(VARN_ENABLE_ZIP)
    get_filename_component(_varn_cmake_dir "${CMAKE_CURRENT_LIST_FILE}" DIRECTORY)
    get_filename_component(_varn_cmake_dir "${_varn_cmake_dir}" DIRECTORY)
    list(PREPEND CMAKE_MODULE_PATH "${_varn_cmake_dir}/modules")

    CPMAddPackage(
        NAME zlib
        VERSION 1.3.1
        GITHUB_REPOSITORY madler/zlib
        GIT_TAG v1.3.1
        OPTIONS
            "ZLIB_BUILD_EXAMPLES OFF"
            "SKIP_INSTALL_ALL ON"
    )
    if(TARGET zlibstatic)
        set_target_properties(zlibstatic PROPERTIES POSITION_INDEPENDENT_CODE ON)
        # ALIAS before find_package so ZLIB::ZLIB always exists even if FPHSA rejects a non-file
        # ZLIB_LIBRARY value; libzip and varn_wasm link ZLIB::ZLIB.
        if(NOT TARGET ZLIB::ZLIB)
            add_library(ZLIB::ZLIB ALIAS zlibstatic)
        endif()
    else()
        message(FATAL_ERROR "varn: zlib CPM did not define target zlibstatic")
    endif()

    # MODULE: avoid Emscripten/SDK CONFIG stubs that set ZLIB_FOUND without defining ZLIB::ZLIB.
    find_package(ZLIB 1.1.2 REQUIRED MODULE)

    # Prefer MODULE FindZLIB (our zlibstatic shim) inside libzip; SDK CONFIG can leave ZLIB::ZLIB missing.
    set(_varn_saved_find_package_prefer_config "${CMAKE_FIND_PACKAGE_PREFER_CONFIG}")
    set(CMAKE_FIND_PACKAGE_PREFER_CONFIG OFF)
    CPMAddPackage(
        NAME libzip
        VERSION 1.10.1
        GITHUB_REPOSITORY nih-at/libzip
        GIT_TAG v1.10.1
        OPTIONS
            "BUILD_SHARED_LIBS OFF"
            "BUILD_TOOLS OFF"
            "BUILD_EXAMPLES OFF"
            "BUILD_DOC OFF"
            "BUILD_REGRESS OFF"
            "LIBZIP_DO_INSTALL OFF"
            "ENABLE_OPENSSL OFF"
            "ENABLE_MBEDTLS OFF"
            "ENABLE_GNUTLS OFF"
            "ENABLE_COMMONCRYPTO OFF"
            "ENABLE_WINDOWS_CRYPTO OFF"
            "ENABLE_BZIP2 OFF"
            "ENABLE_LZMA OFF"
            "ENABLE_ZSTD OFF"
    )
    set(CMAKE_FIND_PACKAGE_PREFER_CONFIG "${_varn_saved_find_package_prefer_config}")
    # Varn sets CMAKE_C_STANDARD to C99 globally. On glibc, <strings.h> only declares strcasecmp
    # when a feature macro such as _DEFAULT_SOURCE is set; libzip's configure checks do not match
    # that compile mode, so zip_name_locate.c can fail with strcasecmp undeclared (e.g. Linux CI).
    if(TARGET zip AND NOT WIN32)
        target_compile_definitions(zip PRIVATE $<$<COMPILE_LANGUAGE:C>:_DEFAULT_SOURCE>)
    endif()
endif()
