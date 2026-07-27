#include "varn/varn.h"

#include <gtest/gtest.h>

#include <cstring>

namespace
{

TEST(CApi, VersionIsNonEmpty)
{
    const char* version = varn_version();
    ASSERT_NE(version, nullptr);
    EXPECT_GT(std::strlen(version), 0u);
}

TEST(CApi, RunStringExecutesAndReturnsZero)
{
    varn_runtime* rt = varn_runtime_new();
    ASSERT_NE(rt, nullptr);

    EXPECT_EQ(varn_runtime_run_string(rt, "local x = 1 + 1", "test-chunk"), 0);

    varn_runtime_free(rt);
}

TEST(CApi, RunStringReportsAScriptError)
{
    varn_runtime* rt = varn_runtime_new();
    ASSERT_NE(rt, nullptr);

    EXPECT_NE(varn_runtime_run_string(rt, "this is not valid lua ==", "bad-chunk"), 0);

    varn_runtime_free(rt);
}

TEST(CApi, GuardsNullArguments)
{
    EXPECT_EQ(varn_runtime_run_file(nullptr, "x.lua"), 2);
    EXPECT_EQ(varn_runtime_run_string(nullptr, "x", "c"), 2);

    varn_runtime* rt = varn_runtime_new();
    ASSERT_NE(rt, nullptr);
    EXPECT_EQ(varn_runtime_run_file(rt, nullptr), 2);
    EXPECT_EQ(varn_runtime_run_string(rt, nullptr, "c"), 2);
    varn_runtime_free(rt);

    // freeing a null runtime is safe
    varn_runtime_free(nullptr);
}

} // namespace
