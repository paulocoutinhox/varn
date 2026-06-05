#pragma once

struct lua_State;

namespace varn::crypto {

class CryptoModule {
public:
    CryptoModule() = delete;

    static void install(struct lua_State* L);

private:
    static bool parseDigestFormat(struct lua_State* L, int index, bool& hexOut);

    static int luaDigest(struct lua_State* L);
    static int luaHmac(struct lua_State* L);
    static int luaRandomBytes(struct lua_State* L);
    static int luaEquals(struct lua_State* L);
    static int luaOpen(struct lua_State* L);
};

} // namespace varn::crypto
