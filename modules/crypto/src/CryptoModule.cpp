#include <lua.hpp>
#include <string_view>

#include "varn/crypto/CryptoModule.h"
#include "varn/crypto/CryptoPrimitives.h"
#include "varn/lua/LuaHelpers.h"

namespace varn::crypto {

bool CryptoModule::parseDigestFormat(lua_State* L, int index, bool& hexOut) {
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

int CryptoModule::luaDigest(lua_State* L) {
    try {
        const std::string algorithm = varn::lua::LuaHelpers::checkString(L, 1);
        const std::string data = varn::lua::LuaHelpers::checkString(L, 2);
        bool hexOut = true;
        if (!parseDigestFormat(L, 3, hexOut)) {
            return luaL_error(L, "[CryptoModule] Output format must be hex or raw.");
        }
        const std::string out = CryptoPrimitives::digest(algorithm, data, hexOut);
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
            return luaL_error(L, "[CryptoModule] Output format must be hex or raw.");
        }
        const std::string out = CryptoPrimitives::hmac(digestAlgo, key, data, hexOut);
        lua_pushlstring(L, out.data(), out.size());
        return 1;
    } catch (const std::exception& ex) {
        return luaL_error(L, "%s", ex.what());
    }
}

int CryptoModule::luaRandomBytes(lua_State* L) {
    try {
        lua_Integer count = luaL_checkinteger(L, 1);
        if (count < 0) {
            return luaL_error(L, "[CryptoModule] Random byte count must not be negative.");
        }
        std::string bytes = CryptoPrimitives::randomBytes(static_cast<std::size_t>(count));
        lua_pushlstring(L, bytes.data(), bytes.size());
        return 1;
    } catch (const std::exception& ex) {
        return luaL_error(L, "%s", ex.what());
    }
}

int CryptoModule::luaEquals(lua_State* L) {
    std::size_t lenA = 0;
    std::size_t lenB = 0;
    const char* a = luaL_checklstring(L, 1, &lenA);
    const char* b = luaL_checklstring(L, 2, &lenB);

    // constant-time comparison so verifying a mac or token does not leak its bytes through timing.
    // a length mismatch folds into the accumulator and reports unequal.
    std::size_t diff = lenA ^ lenB;
    const std::size_t common = lenA < lenB ? lenA : lenB;
    for (std::size_t i = 0; i < common; ++i) {
        diff |= static_cast<unsigned char>(a[i]) ^ static_cast<unsigned char>(b[i]);
    }

    lua_pushboolean(L, diff == 0 ? 1 : 0);
    return 1;
}

int CryptoModule::luaOpen(lua_State* L) {
    lua_newtable(L);

    lua_pushcfunction(L, &CryptoModule::luaDigest);
    lua_setfield(L, -2, "digest");

    lua_pushcfunction(L, &CryptoModule::luaHmac);
    lua_setfield(L, -2, "hmac");

    lua_pushcfunction(L, &CryptoModule::luaRandomBytes);
    lua_setfield(L, -2, "randomBytes");

    lua_pushcfunction(L, &CryptoModule::luaEquals);
    lua_setfield(L, -2, "equals");

    return 1;
}

void CryptoModule::install(lua_State* L) {
    luaL_requiref(L, "crypto", &CryptoModule::luaOpen, 1);
    lua_pop(L, 1);
}

} // namespace varn::crypto
