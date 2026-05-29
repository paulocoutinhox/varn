#pragma once

struct lua_State;

namespace varn::http {

class HttpServerModule {
public:
    HttpServerModule() = delete;

    static void install(struct lua_State* L);
};

} // namespace varn::http
