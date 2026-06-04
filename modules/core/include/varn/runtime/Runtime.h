#pragma once

#include "varn/runtime/EventLoop.h"
#include "varn/runtime/TaskPool.h"
#include "varn/runtime/WorkLedger.h"

#include <atomic>
#include <memory>
#include <string>
#include <vector>

struct lua_State;

namespace varn::http {
class HttpServer;
}

namespace varn::lua {
class LuaEngine;
}

namespace varn::runtime {

class Runtime {
public:
    // scriptArgIndex is the position in args that becomes Lua's arg[0]; later entries become arg[1..]
    // and earlier ones arg[-1..], matching the standard Lua argument convention.
    explicit Runtime(std::vector<std::string> args, std::size_t scriptArgIndex = 1);
    ~Runtime();

    int runScript(const std::string& scriptPath);
    int runString(const std::string& source, const std::string& chunkName);
    bool runStringWithoutEventLoop(const std::string& source, const std::string& chunkName, std::string* errorMessage,
                                   void (*prePcallHook)(lua_State*, void*) = nullptr, void* prePcallUserdata = nullptr);

    EventLoop& mainLoop();
    TaskPool& taskPool();
    lua_State* luaState();

    void addServer(std::shared_ptr<varn::http::HttpServer> server);
    void stop();
    bool stopped() const { return stopped_.load(std::memory_order_acquire); }

    void onAsyncComplete(bool ok, bool stopLoopOnSuccess, const std::string& error);

    void retainBackgroundDriver();
    void releaseBackgroundDriver();

    const std::vector<std::string>& args() const;
    std::size_t scriptArgIndex() const { return scriptArgIndex_; }

private:
    std::vector<std::string> args_;
    std::size_t scriptArgIndex_;
    std::shared_ptr<WorkLedger> workLedger_;
    EventLoop mainLoop_;
    TaskPool taskPool_;
    std::unique_ptr<varn::lua::LuaEngine> lua_;
    std::vector<std::shared_ptr<varn::http::HttpServer>> servers_;
    std::atomic<bool> stopped_{false};
    std::atomic<int> backgroundDrivers_{0};
    bool unhandledError_ = false;
    bool entryRequestedStop_ = false;

    int finishAfterUserChunk(int loadRunExitCode);
};

} // namespace varn::runtime
