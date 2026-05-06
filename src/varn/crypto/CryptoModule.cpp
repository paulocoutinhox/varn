#include <lua.hpp>
#include <string_view>

#include "varn/crypto/CryptoModule.h"
#include "varn/crypto/CryptoPrimitives.h"
#include "varn/lua/LuaHelpers.h"

namespace varn {

namespace {

bool parseDigestFormat(lua_State* L, int index, bool& hexOut) {
    if (lua_gettop(L) < index || !lua_isstring(L, index)) {
        return true;
    }
    const char* fmt = lua_tostring(L, index);
    if (fmt == nullptr) {
        return true;
    }
    if (std::string_view(fmt) == "hex") {
        hexOut = true;
        return true;
    }
    if (std::string_view(fmt) == "raw") {
        hexOut = false;
        return true;
    }
    return false;
}

} // namespace

int CryptoModule::luaDigest(lua_State* L) {
    try {
        const std::string algorithm = varn::lua::LuaHelpers::checkString(L, 1);
        const std::string data = varn::lua::LuaHelpers::checkString(L, 2);
        bool hexOut = true;
        if (!parseDigestFormat(L, 3, hexOut)) {
            return luaL_error(L, "crypto.digest: third argument must be 'hex' or 'raw'");
        }
        const std::string out = crypto::digest(algorithm, data, hexOut);
        lua_pushlstring(L, out.data(), out.size());
        return 1;
    } catch (const std::exception& ex) {
        return luaL_error(L, "%s", ex.what());
    }
}

int CryptoModule::luaHmac(lua_State* L) {
    try {
        const std::string digestAlgo = varn::lua::LuaHelpers::checkString(L, 1);
        const std::string key = varn::lua::LuaHelpers::checkString(L, 2);
        const std::string data = varn::lua::LuaHelpers::checkString(L, 3);
        bool hexOut = true;
        if (!parseDigestFormat(L, 4, hexOut)) {
            return luaL_error(L, "crypto.hmac: fourth argument must be 'hex' or 'raw'");
        }
        const std::string out = crypto::hmac(digestAlgo, key, data, hexOut);
        lua_pushlstring(L, out.data(), out.size());
        return 1;
    } catch (const std::exception& ex) {
        return luaL_error(L, "%s", ex.what());
    }
}

int CryptoModule::luaRandomBytes(lua_State* L) {
    try {
        int count = luaL_checkinteger(L, 1);
        if (count < 0 || count > 1024 * 1024) {
            return luaL_error(L, "invalid random byte count");
        }
        std::string bytes = crypto::randomBytes(static_cast<std::size_t>(count));
        lua_pushlstring(L, bytes.data(), bytes.size());
        return 1;
    } catch (const std::exception& ex) {
        return luaL_error(L, "%s", ex.what());
    }
}

int CryptoModule::luaOpen(lua_State* L) {
    lua_newtable(L);

    lua_pushcfunction(L, &CryptoModule::luaDigest);
    lua_setfield(L, -2, "digest");

    lua_pushcfunction(L, &CryptoModule::luaHmac);
    lua_setfield(L, -2, "hmac");

    lua_pushcfunction(L, &CryptoModule::luaRandomBytes);
    lua_setfield(L, -2, "randomBytes");

    return 1;
}

void CryptoModule::install(lua_State* L) {
    luaL_requiref(L, "crypto", &CryptoModule::luaOpen, 1);
    lua_pop(L, 1);
}

} // namespace varn
