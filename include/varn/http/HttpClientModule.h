#pragma once

struct lua_State;

namespace varn {

class HttpClientModule {
public:
    HttpClientModule() = delete;

    static void registerClient(lua_State* L);

private:
    static int luaClientRequest(lua_State* L);
};

} // namespace varn
