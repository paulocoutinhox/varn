#pragma once

struct lua_State;

namespace varn {

class FfiModule {
public:
    FfiModule() = delete;

    static void install(lua_State* L);
};

} // namespace varn
