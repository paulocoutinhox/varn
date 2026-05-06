#pragma once

#include <lua.hpp>
#include <string>

namespace varn {

class Runtime;

using LuaPrePcallHook = void (*)(lua_State* L, void* userdata);

class LuaEngine {
public:
    explicit LuaEngine(Runtime& runtime);
    ~LuaEngine();

    LuaEngine(const LuaEngine&) = delete;
    LuaEngine& operator=(const LuaEngine&) = delete;

    int runFile(const std::string& path);
    int runString(const std::string& source, const std::string& chunkName);
    /// Run Lua without entering the desktop event loop (WebAssembly one-shot host).
    bool runStringWithoutEventLoop(const std::string& source, const std::string& chunkName, std::string* errorMessage,
                                   LuaPrePcallHook prePcallHook = nullptr, void* prePcallUserdata = nullptr);
    lua_State* state();

private:
    void installNativeModules();
    void configureArgTable();

    Runtime& runtime_;
    lua_State* L_ = nullptr;
};

} // namespace varn
