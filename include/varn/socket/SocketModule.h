#pragma once

struct lua_State;

namespace varn {

class SocketModule {
public:
    SocketModule() = delete;

    static void install(lua_State* L);
};

} // namespace varn
