#pragma once

#include <map>
#include <string>

struct lua_State;

namespace varn::runtime {
class Runtime;
}

namespace varn::http {

class HttpClientModule {
public:
    HttpClientModule() = delete;

    static void registerClient(lua_State* L);

private:
    static int luaClientRequest(lua_State* L);
    static varn::runtime::Runtime& luaRuntime(lua_State* L);
    static void readHeadersTable(lua_State* L, int absIndex, std::map<std::string, std::string>& out);
};

} // namespace varn::http
