# async: sleep/spawn helpers and the promise model resumed on the event loop. no external deps.

list(APPEND VARN_INCLUDE_DIRS "${CMAKE_CURRENT_LIST_DIR}/include")

list(APPEND VARN_SOURCES
    "${CMAKE_CURRENT_LIST_DIR}/src/Promise.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/src/AsyncModule.cpp"
)
