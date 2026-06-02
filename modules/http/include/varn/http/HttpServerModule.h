#pragma once

#include "varn/http/HttpTypes.h"

struct lua_State;

namespace varn::http {

class HttpServerModule {
public:
    HttpServerModule() = delete;

    static void install(struct lua_State* L);
};

HttpServerOptions readListenOptions(lua_State* L, int index);
void pushRequestTable(lua_State* L, const HttpRequest& request);

} // namespace varn::http
