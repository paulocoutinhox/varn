#include <lua.hpp>
#include <stdexcept>
#include <string>
#include <string_view>

#include "varn/crypto/CryptoModule.h"
#include "varn/crypto/CryptoPrimitives.h"
#include "varn/lua/LuaHelpers.h"

#if defined(_MSC_VER)
#define VARN_NOINLINE __declspec(noinline)
#else
#define VARN_NOINLINE __attribute__((noinline))
#endif

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

// each worker owns the try/catch so the entry function below stays free of c++ exception metadata
// and live destructors. msvc's /gs canary aborts the process when lua_error longjmps from a frame
// that ran a c++ try/catch, so the worker returns -1 with the message on top of the lua stack and
// the entry function calls lua_error from a pod-only frame.

VARN_NOINLINE int CryptoModule::luaDigestWorker(lua_State* L) {
    std::size_t algoLen = 0;
    std::size_t dataLen = 0;
    const char* algoRaw = luaL_checklstring(L, 1, &algoLen);
    const char* dataRaw = luaL_checklstring(L, 2, &dataLen);

    try {
        const std::string algorithm(algoRaw, algoLen);
        const std::string data(dataRaw, dataLen);

        bool hexOut = true;
        if (!parseDigestFormat(L, 3, hexOut)) {
            throw std::runtime_error("[CryptoModule] Output format must be hex or raw.");
        }

        const std::string out = CryptoPrimitives::digest(algorithm, data, hexOut);
        lua_pushlstring(L, out.data(), out.size());
        return 1;
    } catch (const std::exception& ex) {
        lua_pushstring(L, ex.what());
    }
    return -1;
}

int CryptoModule::luaDigest(lua_State* L) {
    const int n = luaDigestWorker(L);
    if (n < 0) {
        return lua_error(L);
    }
    return n;
}

VARN_NOINLINE int CryptoModule::luaHmacWorker(lua_State* L) {
    std::size_t algoLen = 0;
    std::size_t keyLen = 0;
    std::size_t dataLen = 0;
    const char* algoRaw = luaL_checklstring(L, 1, &algoLen);
    const char* keyRaw = luaL_checklstring(L, 2, &keyLen);
    const char* dataRaw = luaL_checklstring(L, 3, &dataLen);

    try {
        const std::string digestAlgo(algoRaw, algoLen);
        const std::string key(keyRaw, keyLen);
        const std::string data(dataRaw, dataLen);

        bool hexOut = true;
        if (!parseDigestFormat(L, 4, hexOut)) {
            throw std::runtime_error("[CryptoModule] Output format must be hex or raw.");
        }

        const std::string out = CryptoPrimitives::hmac(digestAlgo, key, data, hexOut);
        lua_pushlstring(L, out.data(), out.size());
        return 1;
    } catch (const std::exception& ex) {
        lua_pushstring(L, ex.what());
    }
    return -1;
}

int CryptoModule::luaHmac(lua_State* L) {
    const int n = luaHmacWorker(L);
    if (n < 0) {
        return lua_error(L);
    }
    return n;
}

VARN_NOINLINE int CryptoModule::luaRandomBytesWorker(lua_State* L) {
    const lua_Integer count = luaL_checkinteger(L, 1);

    try {
        if (count < 0) {
            throw std::runtime_error("[CryptoModule] Random byte count must not be negative.");
        }
        const std::string bytes = CryptoPrimitives::randomBytes(static_cast<std::size_t>(count));
        lua_pushlstring(L, bytes.data(), bytes.size());
        return 1;
    } catch (const std::exception& ex) {
        lua_pushstring(L, ex.what());
    }
    return -1;
}

int CryptoModule::luaRandomBytes(lua_State* L) {
    const int n = luaRandomBytesWorker(L);
    if (n < 0) {
        return lua_error(L);
    }
    return n;
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
