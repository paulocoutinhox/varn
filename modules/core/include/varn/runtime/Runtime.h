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

namespace varn::socket {
class SocketHandle;
}

namespace varn::runtime {

class Runtime {
public:
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
    void registerSocket(const std::shared_ptr<varn::socket::SocketHandle>& socket);
    void stop();
    bool stopped() const { return stopFlag.load(std::memory_order_acquire); }

    void onAsyncComplete(bool ok, bool stopLoopOnSuccess, const std::string& error);

    void retainBackgroundDriver();
    void releaseBackgroundDriver();

    const std::vector<std::string>& args() const;
    std::size_t scriptArgIndex() const { return scriptArgPosition; }

private:
    std::vector<std::string> arguments;
    std::size_t scriptArgPosition;
    std::shared_ptr<WorkLedger> workLedger;
    EventLoop loop;
    TaskPool pool;
    std::unique_ptr<varn::lua::LuaEngine> engine;
    std::vector<std::shared_ptr<varn::http::HttpServer>> servers;
    std::vector<std::weak_ptr<varn::socket::SocketHandle>> sockets;
    std::atomic<bool> stopFlag{false};
    std::atomic<int> backgroundDrivers{0};
    bool unhandledError = false;
    bool entryRequestedStop = false;

    int finishAfterUserChunk(int loadRunExitCode);
};

} // namespace varn::runtime
