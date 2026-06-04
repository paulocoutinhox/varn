# native unit tests: plain googletest, linked against the static core.

CPMAddPackage(
    NAME googletest
    VERSION 1.15.2
    GITHUB_REPOSITORY google/googletest
    GIT_TAG v1.15.2
    OPTIONS
        "INSTALL_GTEST OFF"
        "gtest_force_shared_crt ON"
)

file(GLOB VARN_TEST_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/*.cpp")

add_executable(varn_tests ${VARN_TEST_SOURCES})

# varn_core publishes its include dirs and defines; the module src dirs let the tests reach
# internal helpers (http token/form/range, zip path safety) without widening any public api.
target_link_libraries(varn_tests PRIVATE varn_core GTest::gtest_main)
target_include_directories(varn_tests PRIVATE
    "${VARN_ROOT}/modules/http/src"
    "${VARN_ROOT}/modules/zip/src"
)

add_test(NAME varn_tests COMMAND varn_tests)
