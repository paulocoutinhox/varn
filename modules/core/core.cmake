# core: runtime, lua engine, native module registry, and the shared log pipeline.

# the event loop waits on libuv (epoll/kqueue/iocp) so it can host socket readiness inline; wasm pumps the loop manually instead.
if(NOT VARN_BUILDING_FOR_EMSCRIPTEN)
    set(VARN_NEEDS_POCO ON)
    set(VARN_NEEDS_LIBUV ON)
endif()

# tvos and watchos prohibit fork, so the multi-process worker path is compiled out there like poco's process launch is.
if(CMAKE_SYSTEM_NAME MATCHES "^(tvOS|watchOS|visionOS)$")
    list(APPEND VARN_COMPILE_DEFS "VARN_NO_FORK=1")
endif()

list(APPEND VARN_INCLUDE_DIRS "${CMAKE_CURRENT_LIST_DIR}/include")

list(APPEND VARN_SOURCES
    "${CMAKE_CURRENT_LIST_DIR}/src/runtime/App.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/src/runtime/Runtime.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/src/runtime/EventLoop.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/src/runtime/TaskPool.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/src/lua/LuaEngine.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/src/lua/LuaHelpers.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/src/lua/NativeModules.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/src/log/Log.cpp"
)
