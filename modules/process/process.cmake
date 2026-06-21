# process: run commands and read the environment through a platform driver.

if(NOT DEFINED CACHE{VARN_PROCESS_DRIVER})
    # fork/exec and unistd are unavailable on emscripten, windows, and the apple tv/watch/vision platforms.
    if(VARN_BUILDING_FOR_EMSCRIPTEN OR WIN32 OR CMAKE_SYSTEM_NAME MATCHES "^(tvOS|watchOS|visionOS)$")
        set(VARN_PROCESS_DRIVER "DUMMY" CACHE STRING "process backend: POSIX DUMMY")
    else()
        set(VARN_PROCESS_DRIVER "POSIX" CACHE STRING "process backend: POSIX DUMMY")
    endif()
endif()
set_property(CACHE VARN_PROCESS_DRIVER PROPERTY STRINGS POSIX DUMMY)
varn_validate_driver(VARN_PROCESS_DRIVER POSIX DUMMY)

list(APPEND VARN_INCLUDE_DIRS "${CMAKE_CURRENT_LIST_DIR}/include")
list(APPEND VARN_SOURCES "${CMAKE_CURRENT_LIST_DIR}/src/ProcessModule.cpp")

if(VARN_PROCESS_DRIVER STREQUAL "POSIX")
    list(APPEND VARN_SOURCES "${CMAKE_CURRENT_LIST_DIR}/src/drivers/posix/PosixProcessRunner.cpp")
else()
    list(APPEND VARN_SOURCES "${CMAKE_CURRENT_LIST_DIR}/src/drivers/dummy/DummyProcessRunner.cpp")
endif()

varn_register_driver(PROCESS "${VARN_PROCESS_DRIVER}")
