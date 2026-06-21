#include "varn/fs/FsModule.h"
#include "varn/async/Promise.h"
#include "varn/fs/FsStorage.h"
#include "varn/lua/LuaHelpers.h"
#include "varn/runtime/Runtime.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace varn::fs
{

using varn::async::Promise;
using varn::runtime::Runtime;

namespace
{
constexpr const char* kFsHandleMeta = "varn.FsHandle";

Runtime& fsRuntime(lua_State* L)
{
    return *static_cast<Runtime*>(varn::lua::LuaHelpers::getRuntime(L));
}

std::string checkPath(lua_State* L, int index)
{
    std::string path = varn::lua::LuaHelpers::checkString(L, index);

    // a null byte truncates the path at the os boundary, so reject it rather than act on a different path than was asked for.
    if (path.find('\0') != std::string::npos)
    {
        luaL_error(L, "[FsModule] A path must not contain a null byte.");
    }

    return path;
}

void pushHandle(lua_State* L, std::shared_ptr<FsHandle> handle)
{
    void* memory = lua_newuserdatauv(L, sizeof(std::shared_ptr<FsHandle>), 0);
    new (memory) std::shared_ptr<FsHandle>(std::move(handle));
    luaL_getmetatable(L, kFsHandleMeta);
    lua_setmetatable(L, -2);
}

std::shared_ptr<FsHandle>& checkHandle(lua_State* L)
{
    return *static_cast<std::shared_ptr<FsHandle>*>(luaL_checkudata(L, 1, kFsHandleMeta));
}

int luaHandleGc(lua_State* L)
{
    auto* holder = static_cast<std::shared_ptr<FsHandle>*>(lua_touserdata(L, 1));
    if (holder != nullptr)
    {
        std::destroy_at(holder);
    }

    return 0;
}

int luaHandleRead(lua_State* L)
{
    auto handle = checkHandle(L);
    const lua_Integer maxArg = luaL_optinteger(L, 2, 65536);
    if (maxArg <= 0)
    {
        return luaL_error(L, "[FsModule] The maximum number of bytes to read must be positive.");
    }

    const std::size_t maxBytes = static_cast<std::size_t>(maxArg);
    auto& rt = fsRuntime(L);
    auto promise = std::make_shared<Promise>(rt);

    rt.ioPool().post([promise, handle, maxBytes]
                     {
        try
        {
            promise->resolve(handle->read(maxBytes));
        }
        catch (const std::exception& ex)
        {
            promise->reject(ex.what());
        }
        catch (...)
        {
            promise->reject("[FsModule] The operation failed with a non-standard error.");
        } });

    Promise::push(L, promise);
    return 1;
}

int luaHandleWrite(lua_State* L)
{
    auto handle = checkHandle(L);
    std::size_t len = 0;
    const char* raw = luaL_checklstring(L, 2, &len);
    std::string data(raw, len);
    auto& rt = fsRuntime(L);
    auto promise = std::make_shared<Promise>(rt);

    rt.ioPool().post([promise, handle, data = std::move(data)]
                     {
        try
        {
            handle->write(data);
            promise->resolve("ok");
        }
        catch (const std::exception& ex)
        {
            promise->reject(ex.what());
        }
        catch (...)
        {
            promise->reject("[FsModule] The operation failed with a non-standard error.");
        } });

    Promise::push(L, promise);
    return 1;
}

int luaHandleClose(lua_State* L)
{
    auto handle = checkHandle(L);
    auto& rt = fsRuntime(L);
    auto promise = std::make_shared<Promise>(rt);

    rt.ioPool().post([promise, handle]
                     {
        try
        {
            handle->close();
            promise->resolve("ok");
        }
        catch (const std::exception& ex)
        {
            promise->reject(ex.what());
        }
        catch (...)
        {
            promise->reject("[FsModule] The operation failed with a non-standard error.");
        } });

    Promise::push(L, promise);
    return 1;
}

int luaFileOpen(lua_State* L)
{
    std::string path = checkPath(L, 1);
    std::string mode = varn::lua::LuaHelpers::optionalString(L, 2, "r");
    auto& rt = fsRuntime(L);
    auto promise = std::make_shared<Promise>(rt);

    rt.ioPool().post([promise, path = std::move(path), mode = std::move(mode)]
                     {
        try
        {
            std::shared_ptr<FsHandle> handle = FsStorage::open(path, mode);
            promise->resolveCustom([handle](lua_State* lua)
                                   { pushHandle(lua, handle); });
        }
        catch (const std::exception& ex)
        {
            promise->reject(ex.what());
        }
        catch (...)
        {
            promise->reject("[FsModule] The operation failed with a non-standard error.");
        } });

    Promise::push(L, promise);
    return 1;
}

int luaStat(lua_State* L)
{
    std::string path = checkPath(L, 1);
    auto& rt = fsRuntime(L);
    auto promise = std::make_shared<Promise>(rt);

    rt.ioPool().post([promise, path = std::move(path)]
                     {
        try
        {
            FsStat info = FsStorage::stat(path);
            promise->resolveCustom([info](lua_State* lua)
                                   {
                lua_createtable(lua, 0, 5);
                lua_pushinteger(lua, static_cast<lua_Integer>(info.size));
                lua_setfield(lua, -2, "size");
                lua_pushinteger(lua, static_cast<lua_Integer>(info.mtime));
                lua_setfield(lua, -2, "mtime");
                lua_pushboolean(lua, info.isDir);
                lua_setfield(lua, -2, "isDir");
                lua_pushboolean(lua, info.isFile);
                lua_setfield(lua, -2, "isFile");
                lua_pushboolean(lua, info.isSymlink);
                lua_setfield(lua, -2, "isSymlink"); });
        }
        catch (const std::exception& ex)
        {
            promise->reject(ex.what());
        }
        catch (...)
        {
            promise->reject("[FsModule] The operation failed with a non-standard error.");
        } });

    Promise::push(L, promise);
    return 1;
}

int luaReaddir(lua_State* L)
{
    std::string path = checkPath(L, 1);
    auto& rt = fsRuntime(L);
    auto promise = std::make_shared<Promise>(rt);

    rt.ioPool().post([promise, path = std::move(path)]
                     {
        try
        {
            std::vector<std::string> names = FsStorage::readdir(path);
            promise->resolveCustom([names = std::move(names)](lua_State* lua)
                                   {
                lua_createtable(lua, static_cast<int>(names.size()), 0);
                for (std::size_t i = 0; i < names.size(); ++i)
                {
                    lua_pushlstring(lua, names[i].data(), names[i].size());
                    lua_rawseti(lua, -2, static_cast<lua_Integer>(i + 1));
                } });
        }
        catch (const std::exception& ex)
        {
            promise->reject(ex.what());
        }
        catch (...)
        {
            promise->reject("[FsModule] The operation failed with a non-standard error.");
        } });

    Promise::push(L, promise);
    return 1;
}

int luaRename(lua_State* L)
{
    std::string from = checkPath(L, 1);
    std::string to = checkPath(L, 2);
    auto& rt = fsRuntime(L);
    auto promise = std::make_shared<Promise>(rt);

    rt.ioPool().post([promise, from = std::move(from), to = std::move(to)]
                     {
        try
        {
            FsStorage::rename(from, to);
            promise->resolve("ok");
        }
        catch (const std::exception& ex)
        {
            promise->reject(ex.what());
        }
        catch (...)
        {
            promise->reject("[FsModule] The operation failed with a non-standard error.");
        } });

    Promise::push(L, promise);
    return 1;
}

int luaCopy(lua_State* L)
{
    std::string from = checkPath(L, 1);
    std::string to = checkPath(L, 2);
    auto& rt = fsRuntime(L);
    auto promise = std::make_shared<Promise>(rt);

    rt.ioPool().post([promise, from = std::move(from), to = std::move(to)]
                     {
        try
        {
            FsStorage::copy(from, to);
            promise->resolve("ok");
        }
        catch (const std::exception& ex)
        {
            promise->reject(ex.what());
        }
        catch (...)
        {
            promise->reject("[FsModule] The operation failed with a non-standard error.");
        } });

    Promise::push(L, promise);
    return 1;
}

int luaAppend(lua_State* L)
{
    std::string path = checkPath(L, 1);
    std::size_t len = 0;
    const char* raw = luaL_checklstring(L, 2, &len);
    std::string data(raw, len);
    auto& rt = fsRuntime(L);
    auto promise = std::make_shared<Promise>(rt);

    rt.ioPool().post([promise, path = std::move(path), data = std::move(data)]
                     {
        try
        {
            FsStorage::append(path, data);
            promise->resolve("ok");
        }
        catch (const std::exception& ex)
        {
            promise->reject(ex.what());
        }
        catch (...)
        {
            promise->reject("[FsModule] The operation failed with a non-standard error.");
        } });

    Promise::push(L, promise);
    return 1;
}

int luaMkdtemp(lua_State* L)
{
    std::string prefix = checkPath(L, 1);
    auto& rt = fsRuntime(L);
    auto promise = std::make_shared<Promise>(rt);

    rt.ioPool().post([promise, prefix = std::move(prefix)]
                     {
        try
        {
            promise->resolve(FsStorage::mkdtemp(prefix));
        }
        catch (const std::exception& ex)
        {
            promise->reject(ex.what());
        }
        catch (...)
        {
            promise->reject("[FsModule] The operation failed with a non-standard error.");
        } });

    Promise::push(L, promise);
    return 1;
}

void createHandleMetatable(lua_State* L)
{
    if (luaL_newmetatable(L, kFsHandleMeta))
    {
        lua_pushcfunction(L, &luaHandleGc);
        lua_setfield(L, -2, "__gc");

        lua_newtable(L);
        lua_pushcfunction(L, &luaHandleRead);
        lua_setfield(L, -2, "read");
        lua_pushcfunction(L, &luaHandleWrite);
        lua_setfield(L, -2, "write");
        lua_pushcfunction(L, &luaHandleClose);
        lua_setfield(L, -2, "close");
        lua_setfield(L, -2, "__index");
    }

    lua_pop(L, 1);
}
} // namespace

Runtime& FsModule::luaRuntime(lua_State* L)
{
    return *static_cast<Runtime*>(varn::lua::LuaHelpers::getRuntime(L));
}

int FsModule::luaReadFile(lua_State* L)
{
    auto& rt = luaRuntime(L);
    std::string path = checkPath(L, 1);
    auto promise = std::make_shared<Promise>(rt);

    rt.ioPool().post([promise, path = std::move(path)]
                     {
        try {
            promise->resolve(FsStorage::readAll(path));
        } catch (const std::exception& ex) {
            promise->reject(ex.what());
        } catch (...) {
            // a worker thread must never let an exception escape and terminate the process.
            promise->reject("[FsModule] The operation failed with a non-standard error.");
        } });

    Promise::push(L, promise);
    return 1;
}

int FsModule::luaWriteFile(lua_State* L)
{
    auto& rt = luaRuntime(L);
    std::string path = checkPath(L, 1);
    std::string content = varn::lua::LuaHelpers::checkString(L, 2);
    auto promise = std::make_shared<Promise>(rt);

    rt.ioPool().post([promise, path = std::move(path), content = std::move(content)]
                     {
        try {
            FsStorage::writeAll(path, content);
            promise->resolve("ok");
        } catch (const std::exception& ex) {
            promise->reject(ex.what());
        } catch (...) {
            // a worker thread must never let an exception escape and terminate the process.
            promise->reject("[FsModule] The operation failed with a non-standard error.");
        } });

    Promise::push(L, promise);
    return 1;
}

int FsModule::luaExists(lua_State* L)
{
    std::string path = checkPath(L, 1);
    lua_pushboolean(L, FsStorage::exists(path));
    return 1;
}

int FsModule::luaMkdir(lua_State* L)
{
    auto& rt = luaRuntime(L);
    std::string path = checkPath(L, 1);
    auto promise = std::make_shared<Promise>(rt);

    rt.ioPool().post([promise, path = std::move(path)]
                     {
        try {
            FsStorage::mkdir(path);
            promise->resolve("ok");
        } catch (const std::exception& ex) {
            promise->reject(ex.what());
        } catch (...) {
            promise->reject("[FsModule] The operation failed with a non-standard error.");
        } });

    Promise::push(L, promise);
    return 1;
}

int FsModule::luaRemoveRecursive(lua_State* L)
{
    auto& rt = luaRuntime(L);
    std::string path = checkPath(L, 1);
    auto promise = std::make_shared<Promise>(rt);

    rt.ioPool().post([promise, path = std::move(path)]
                     {
        try {
            FsStorage::removeRecursive(path);
            promise->resolve("ok");
        } catch (const std::exception& ex) {
            promise->reject(ex.what());
        } catch (...) {
            promise->reject("[FsModule] The operation failed with a non-standard error.");
        } });

    Promise::push(L, promise);
    return 1;
}

int FsModule::luaOpen(lua_State* L)
{
    createHandleMetatable(L);

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

    lua_pushcfunction(L, &luaFileOpen);
    lua_setfield(L, -2, "open");

    lua_pushcfunction(L, &luaStat);
    lua_setfield(L, -2, "stat");

    lua_pushcfunction(L, &luaReaddir);
    lua_setfield(L, -2, "readdir");

    lua_pushcfunction(L, &luaRename);
    lua_setfield(L, -2, "rename");

    lua_pushcfunction(L, &luaCopy);
    lua_setfield(L, -2, "copy");

    lua_pushcfunction(L, &luaAppend);
    lua_setfield(L, -2, "append");

    lua_pushcfunction(L, &luaMkdtemp);
    lua_setfield(L, -2, "mkdtemp");

    return 1;
}

void FsModule::install(lua_State* L)
{
    luaL_requiref(L, "fs", &FsModule::luaOpen, 1);
    lua_pop(L, 1);
}

} // namespace varn::fs
