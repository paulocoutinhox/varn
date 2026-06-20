#pragma once

struct lua_State;

namespace varn::crypto
{

class CryptoModule
{
public:
    CryptoModule() = delete;

    static void install(lua_State* L);

private:
    static bool parseDigestFormat(lua_State* L, int index, bool& hexOut);

    static int luaDigest(lua_State* L);
    static int luaHmac(lua_State* L);
    static int luaRandomBytes(lua_State* L);
    static int luaEquals(lua_State* L);
    static int luaOpen(lua_State* L);
};

} // namespace varn::crypto
