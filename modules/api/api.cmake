# api: the public C embedding surface (varn.h) that wraps the runtime for host apps.

list(APPEND VARN_INCLUDE_DIRS "${CMAKE_CURRENT_LIST_DIR}/include")
list(APPEND VARN_SOURCES "${CMAKE_CURRENT_LIST_DIR}/src/Api.cpp")
