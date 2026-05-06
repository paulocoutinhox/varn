#pragma once

struct lua_State;

namespace varn {

class HttpServerModule {
public:
    HttpServerModule() = delete;

    static void install(struct lua_State* L);
};

} // namespace varn
