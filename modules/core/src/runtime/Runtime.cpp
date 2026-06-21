#include "varn/runtime/Runtime.h"
#include "varn/http/HttpTypes.h"
#include "varn/log/Log.h"
#include "varn/lua/LuaEngine.h"

namespace varn::runtime
{

using varn::lua::LuaEngine;

namespace
{
// size a dedicated pool for blocking i/o (http client, filesystem) so it never starves the cpu task pool
constexpr std::size_t kIoThreads = 32;
} // namespace

Runtime::Runtime(std::vector<std::string> args, std::size_t scriptArgIndex)
    : arguments(std::move(args))
    , scriptArgPosition(scriptArgIndex)
    , workLedger(std::make_shared<WorkLedger>())
    , loop(workLedger)
    , pool(std::thread::hardware_concurrency(), workLedger)
    , engine(std::make_unique<LuaEngine>(*this))
{
    // install the notify hook before any worker spins up so none can observe it unset
    workLedger->setNotify([this]
                          { loop.wake(); });
    pool.start();
}

Runtime::~Runtime()
{
    stop();
}

int Runtime::runScript(const std::string& scriptPath)
{
    return finishAfterUserChunk(engine->runFile(scriptPath));
}

int Runtime::runString(const std::string& source, const std::string& chunkName)
{
    return finishAfterUserChunk(engine->runString(source, chunkName));
}

bool Runtime::runStringWithoutEventLoop(const std::string& source, const std::string& chunkName, std::string* errorMessage, void (*prePcallHook)(lua_State*, void*), void* prePcallUserdata)
{
    return engine->runStringWithoutEventLoop(source, chunkName, errorMessage, prePcallHook, prePcallUserdata);
}

int Runtime::finishAfterUserChunk(int loadRunExitCode)
{
    if (loadRunExitCode != 0)
    {
        return loadRunExitCode;
    }

    // handle an async entry that already failed or completed synchronously before the loop starts
    if (unhandledError)
    {
        return 1;
    }

    if (entryRequestedStop)
    {
        return 0;
    }

    loop.setIdleExitPredicate([this]
                              { return servers.empty() && backgroundDrivers.load(std::memory_order_acquire) == 0 && workLedger->depth() == 0; });
    loop.run();
    loop.setIdleExitPredicate({});
    return unhandledError ? 1 : 0;
}

void Runtime::onAsyncComplete(bool ok, bool stopLoopOnSuccess, const std::string& error)
{
    if (!ok)
    {
        unhandledError = true;
        log::Log::error("Runtime", error.empty() ? "A task failed without a message." : error);
        loop.stop();
        return;
    }

    if (stopLoopOnSuccess)
    {
        entryRequestedStop = true;
        loop.stop();
    }
}

EventLoop& Runtime::mainLoop()
{
    return loop;
}

TaskPool& Runtime::taskPool()
{
    return pool;
}

TaskPool& Runtime::ioPool()
{
    if (!ioWorkers)
    {
        ioWorkers = std::make_unique<TaskPool>(kIoThreads, workLedger);
        ioWorkers->start();
    }

    return *ioWorkers;
}

lua_State* Runtime::luaState()
{
    return engine->state();
}

void Runtime::addServer(std::shared_ptr<varn::http::HttpServer> server)
{
    servers.push_back(std::move(server));
    loop.wake();
}

void Runtime::retainBackgroundDriver()
{
    backgroundDrivers.fetch_add(1, std::memory_order_relaxed);
}

void Runtime::releaseBackgroundDriver()
{
    backgroundDrivers.fetch_sub(1, std::memory_order_acq_rel);
    loop.wake();
}

void Runtime::stop()
{
    bool expected = false;
    if (!stopFlag.compare_exchange_strong(expected, true))
    {
        return;
    }

    for (auto& server : servers)
    {
        if (server)
        {
            server->stop();
        }
    }

    pool.stop();
    if (ioWorkers)
    {
        ioWorkers->stop();
    }

    loop.stop();
    // drop still-queued jobs to release their captured state such as lua registry refs while the lua_State is still alive
    loop.clearPendingJobs();
}

const std::vector<std::string>& Runtime::args() const
{
    return arguments;
}

} // namespace varn::runtime
