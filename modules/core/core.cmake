# core: runtime, lua engine, native module registry, and the shared log pipeline. no external deps.

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
