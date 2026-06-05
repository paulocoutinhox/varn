#include "varn/zip/ZipModule.h"

#include "ZipPath.h"
#include "varn/async/Promise.h"
#include "varn/lua/LuaHelpers.h"
#include "varn/runtime/Runtime.h"

#include <lua.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#if defined(VARN_HAVE_LIBZIP) && VARN_HAVE_LIBZIP
#include <zip.h>
#endif

namespace varn::zip {

namespace fs = std::filesystem;

using varn::runtime::Runtime;
using varn::async::Promise;

#if defined(VARN_HAVE_LIBZIP) && VARN_HAVE_LIBZIP

Runtime& ZipModule::luaRuntime(lua_State* L) {
    return *static_cast<Runtime*>(varn::lua::LuaHelpers::getRuntime(L));
}

void ZipModule::performExtract(const std::string& zipPath, const std::string& destDir) {
    int err = 0;
    zip_t* za = zip_open(zipPath.c_str(), ZIP_RDONLY, &err);
    if (!za) {
        throw std::runtime_error("[ZipModule] The archive could not be opened.");
    }

    fs::create_directories(destDir);
    const fs::path destRoot = fs::canonical(fs::absolute(destDir));

    // bound the entry count and the total expansion so a zip bomb cannot exhaust disk or inodes.
    constexpr zip_uint64_t kMaxEntries = 100000;
    constexpr std::uint64_t kMaxTotalBytes = 2ull * 1024 * 1024 * 1024;

    const zip_int64_t n = zip_get_num_entries(za, 0);
    if (n < 0 || static_cast<zip_uint64_t>(n) > kMaxEntries) {
        zip_discard(za);
        throw std::runtime_error("[ZipModule] The archive declares too many entries.");
    }

    std::uint64_t totalWritten = 0;
    for (zip_uint64_t i = 0; i < static_cast<zip_uint64_t>(n); ++i) {
        const char* name = zip_get_name(za, i, ZIP_FL_ENC_GUESS);
        if (name == nullptr) {
            continue;
        }
        const std::string nm(name);
        if (!ZipPath::entryPathSafe(nm)) {
            zip_discard(za);
            throw std::runtime_error("[ZipModule] The archive contains an unsafe entry name.");
        }

        const fs::path outFile = destRoot / nm;
        const fs::path parentDir = outFile.parent_path();
        fs::create_directories(parentDir);

        const fs::path canonParent = fs::weakly_canonical(parentDir);
        if (!ZipPath::isSubpath(destRoot, canonParent)) {
            zip_discard(za);
            throw std::runtime_error("[ZipModule] An entry tried to escape the destination directory.");
        }

        if (!nm.empty() && nm.back() == '/') {
            continue;
        }

        zip_file_t* zf = zip_fopen_index(za, i, 0);
        if (!zf) {
            zip_discard(za);
            throw std::runtime_error("[ZipModule] An entry inside the archive could not be opened.");
        }

        std::ofstream out(outFile, std::ios::binary);
        if (!out) {
            zip_fclose(zf);
            zip_discard(za);
            throw std::runtime_error("[ZipModule] An output file could not be created.");
        }

        char buf[8192];
        while (true) {
            const zip_int64_t r = zip_fread(zf, buf, sizeof(buf));
            if (r < 0) {
                zip_fclose(zf);
                zip_discard(za);
                throw std::runtime_error("[ZipModule] An entry could not be read from the archive.");
            }
            if (r == 0) {
                break;
            }
            totalWritten += static_cast<std::uint64_t>(r);
            if (totalWritten > kMaxTotalBytes) {
                zip_fclose(zf);
                zip_discard(za);
                throw std::runtime_error("[ZipModule] The archive expands beyond the allowed size.");
            }
            out.write(buf, static_cast<std::size_t>(r));
        }
        zip_fclose(zf);
    }

    if (zip_close(za) != 0) {
        throw std::runtime_error("[ZipModule] The archive could not be closed after extraction.");
    }
}

void ZipModule::performCreate(const std::string& zipPath, const std::vector<std::pair<std::string, std::string>>& items) {
    int err = 0;
    zip_t* za = zip_open(zipPath.c_str(), ZIP_CREATE | ZIP_TRUNCATE, &err);
    if (!za) {
        throw std::runtime_error("[ZipModule] The archive could not be created.");
    }

    for (const auto& [localPath, entryName] : items) {
        if (!ZipPath::entryPathSafe(entryName)) {
            zip_discard(za);
            throw std::runtime_error("[ZipModule] An entry name is unsafe.");
        }
        if (!fs::is_regular_file(localPath)) {
            zip_discard(za);
            throw std::runtime_error("[ZipModule] A source path is not a regular file.");
        }

        zip_source_t* s = zip_source_file(za, localPath.c_str(), 0, 0);
        if (!s) {
            zip_discard(za);
            throw std::runtime_error("[ZipModule] A source file could not be read.");
        }
        const zip_int64_t idx = zip_file_add(za, entryName.c_str(), s, ZIP_FL_ENC_UTF_8);
        if (idx < 0) {
            zip_source_free(s);
            zip_discard(za);
            throw std::runtime_error("[ZipModule] An entry could not be added to the archive.");
        }
    }

    if (zip_close(za) != 0) {
        throw std::runtime_error("[ZipModule] The archive could not be closed after creation.");
    }
}

std::vector<std::string> ZipModule::performList(const std::string& zipPath) {
    int err = 0;
    zip_t* za = zip_open(zipPath.c_str(), ZIP_RDONLY, &err);
    if (!za) {
        throw std::runtime_error("[ZipModule] The archive could not be opened.");
    }

    const zip_int64_t n = zip_get_num_entries(za, 0);
    if (n < 0) {
        zip_discard(za);
        throw std::runtime_error("[ZipModule] The archive directory could not be read.");
    }

    std::vector<std::string> names;
    for (zip_uint64_t i = 0; i < static_cast<zip_uint64_t>(n); ++i) {
        const char* name = zip_get_name(za, i, ZIP_FL_ENC_GUESS);
        if (name == nullptr) {
            continue;
        }
        const std::string nm(name);
        if (!ZipPath::entryPathSafe(nm)) {
            zip_discard(za);
            throw std::runtime_error("[ZipModule] The archive contains an unsafe entry name.");
        }
        names.push_back(nm);
    }

    if (zip_close(za) != 0) {
        throw std::runtime_error("[ZipModule] The archive could not be closed after listing.");
    }
    return names;
}

#endif

int ZipModule::luaExtract(lua_State* L) {
#if defined(VARN_HAVE_LIBZIP) && VARN_HAVE_LIBZIP
    const std::string zipPath = varn::lua::LuaHelpers::checkString(L, 1);
    const std::string destDir = varn::lua::LuaHelpers::checkString(L, 2);
    auto& rt = luaRuntime(L);
    auto promise = std::make_shared<Promise>(rt);
    rt.taskPool().post([promise, zipPath, destDir] {
        try {
            performExtract(zipPath, destDir);
            promise->resolve("ok");
        } catch (const std::exception& ex) {
            promise->reject(ex.what());
        } catch (...) {
            // a worker thread must never let an exception escape: that would terminate the process.
            promise->reject("[ZipModule] The operation failed with a non-standard error.");
        }
    });
    Promise::push(L, promise);
    return 1;
#else
    return luaL_error(L, "[ZipModule] The zip module is not available in this build.");
#endif
}

int ZipModule::luaCreate(lua_State* L) {
#if defined(VARN_HAVE_LIBZIP) && VARN_HAVE_LIBZIP
    const std::string zipPath = varn::lua::LuaHelpers::checkString(L, 1);
    luaL_checktype(L, 2, LUA_TTABLE);
    const int len = static_cast<int>(lua_rawlen(L, 2));
    if (len <= 0) {
        return luaL_error(L, "[ZipModule] The list of entries must not be empty.");
    }

    std::vector<std::pair<std::string, std::string>> items;
    items.reserve(static_cast<std::size_t>(len));

    for (int i = 1; i <= len; ++i) {
        lua_rawgeti(L, 2, i);
        if (!lua_istable(L, -1)) {
            lua_pop(L, 1);
            return luaL_error(L, "[ZipModule] Each entry must be a table.");
        }
        lua_getfield(L, -1, "file");
        const std::string file = varn::lua::LuaHelpers::checkString(L, -1);
        lua_pop(L, 1);
        lua_getfield(L, -1, "entry");
        const std::string entry = varn::lua::LuaHelpers::checkString(L, -1);
        lua_pop(L, 1);
        lua_pop(L, 1);
        items.emplace_back(file, entry);
    }

    auto& rt = luaRuntime(L);
    auto promise = std::make_shared<Promise>(rt);
    rt.taskPool().post([promise, zipPath, items = std::move(items)] {
        try {
            performCreate(zipPath, items);
            promise->resolve("ok");
        } catch (const std::exception& ex) {
            promise->reject(ex.what());
        } catch (...) {
            // a worker thread must never let an exception escape: that would terminate the process.
            promise->reject("[ZipModule] The operation failed with a non-standard error.");
        }
    });
    Promise::push(L, promise);
    return 1;
#else
    return luaL_error(L, "[ZipModule] The zip module is not available in this build.");
#endif
}

int ZipModule::luaList(lua_State* L) {
#if defined(VARN_HAVE_LIBZIP) && VARN_HAVE_LIBZIP
    const std::string zipPath = varn::lua::LuaHelpers::checkString(L, 1);
    auto& rt = luaRuntime(L);
    auto promise = std::make_shared<Promise>(rt);
    rt.taskPool().post([promise, zipPath] {
        try {
            std::vector<std::string> names = performList(zipPath);
            promise->resolveCustom([names = std::move(names)](lua_State* lua) {
                lua_createtable(lua, static_cast<int>(names.size()), 0);
                int idx = 1;
                for (const auto& s : names) {
                    lua_pushlstring(lua, s.data(), s.size());
                    lua_rawseti(lua, -2, idx);
                    ++idx;
                }
            });
        } catch (const std::exception& ex) {
            promise->reject(ex.what());
        } catch (...) {
            // a worker thread must never let an exception escape: that would terminate the process.
            promise->reject("[ZipModule] The operation failed with a non-standard error.");
        }
    });
    Promise::push(L, promise);
    return 1;
#else
    return luaL_error(L, "[ZipModule] The zip module is not available in this build.");
#endif
}

int ZipModule::luaOpen(lua_State* L) {
    lua_newtable(L);
    lua_pushcfunction(L, &ZipModule::luaExtract);
    lua_setfield(L, -2, "extract");
    lua_pushcfunction(L, &ZipModule::luaCreate);
    lua_setfield(L, -2, "create");
    lua_pushcfunction(L, &ZipModule::luaList);
    lua_setfield(L, -2, "list");
    return 1;
}

void ZipModule::install(lua_State* L) {
    luaL_requiref(L, "zip", &ZipModule::luaOpen, 1);
    lua_pop(L, 1);
}

} // namespace varn::zip
