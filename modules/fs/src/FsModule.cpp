#include "varn/fs/FsModule.h"
#include "varn/fs/FsStorage.h"
#include "varn/async/Promise.h"
#include "varn/lua/LuaHelpers.h"
#include "varn/runtime/Runtime.h"

#include <memory>
#include <string>

namespace varn::fs {

using varn::runtime::Runtime;
using varn::async::Promise;

Runtime& FsModule::luaRuntime(lua_State* L) {
    return *static_cast<Runtime*>(varn::lua::LuaHelpers::getRuntime(L));
}

int FsModule::luaReadFile(lua_State* L) {
    auto& rt = luaRuntime(L);
    std::string path = varn::lua::LuaHelpers::checkString(L, 1);
    auto promise = std::make_shared<Promise>(rt);

    rt.ioPool().post([promise, path = std::move(path)] {
        try {
            promise->resolve(FsStorage::readAll(path));
        } catch (const std::exception& ex) {
            promise->reject(ex.what());
        } catch (...) {
            // a worker thread must never let an exception escape and terminate the process.
            promise->reject("[FsModule] The operation failed with a non-standard error.");
        }
    });

    Promise::push(L, promise);
    return 1;
}

int FsModule::luaWriteFile(lua_State* L) {
    auto& rt = luaRuntime(L);
    std::string path = varn::lua::LuaHelpers::checkString(L, 1);
    std::string content = varn::lua::LuaHelpers::checkString(L, 2);
    auto promise = std::make_shared<Promise>(rt);

    rt.ioPool().post([promise, path = std::move(path), content = std::move(content)] {
        try {
            FsStorage::writeAll(path, content);
            promise->resolve("ok");
        } catch (const std::exception& ex) {
            promise->reject(ex.what());
        } catch (...) {
            // a worker thread must never let an exception escape and terminate the process.
            promise->reject("[FsModule] The operation failed with a non-standard error.");
        }
    });

    Promise::push(L, promise);
    return 1;
}

int FsModule::luaExists(lua_State* L) {
    std::string path = varn::lua::LuaHelpers::checkString(L, 1);
    lua_pushboolean(L, FsStorage::exists(path));
    return 1;
}

int FsModule::luaMkdir(lua_State* L) {
    auto& rt = luaRuntime(L);
    std::string path = varn::lua::LuaHelpers::checkString(L, 1);
    auto promise = std::make_shared<Promise>(rt);

    rt.ioPool().post([promise, path = std::move(path)] {
        try {
            FsStorage::mkdir(path);
            promise->resolve("ok");
        } catch (const std::exception& ex) {
            promise->reject(ex.what());
        } catch (...) {
            promise->reject("[FsModule] The operation failed with a non-standard error.");
        }
    });

    Promise::push(L, promise);
    return 1;
}

int FsModule::luaRemoveRecursive(lua_State* L) {
    auto& rt = luaRuntime(L);
    std::string path = varn::lua::LuaHelpers::checkString(L, 1);
    auto promise = std::make_shared<Promise>(rt);

    rt.ioPool().post([promise, path = std::move(path)] {
        try {
            FsStorage::removeRecursive(path);
            promise->resolve("ok");
        } catch (const std::exception& ex) {
            promise->reject(ex.what());
        } catch (...) {
            promise->reject("[FsModule] The operation failed with a non-standard error.");
        }
    });

    Promise::push(L, promise);
    return 1;
}

int FsModule::luaOpen(lua_State* L) {
    lua_newtable(L);

    lua_pushcfunction(L, &FsModule::luaReadFile);
    lua_setfield(L, -2, "readFile");

    lua_pushcfunction(L, &FsModule::luaWriteFile);
    lua_setfield(L, -2, "writeFile");

    lua_pushcfunction(L, &FsModule::luaExists);
    lua_setfield(L, -2, "exists");

    lua_pushcfunction(L, &FsModule::luaMkdir);
    lua_setfield(L, -2, "mkdir");

    lua_pushcfunction(L, &FsModule::luaRemoveRecursive);
    lua_setfield(L, -2, "removeRecursive");

    return 1;
}

void FsModule::install(lua_State* L) {
    luaL_requiref(L, "fs", &FsModule::luaOpen, 1);
    lua_pop(L, 1);
}

} // namespace varn::fs
