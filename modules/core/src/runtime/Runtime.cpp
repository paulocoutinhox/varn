#include "varn/runtime/Runtime.h"
#include "varn/lua/LuaEngine.h"
#include "varn/log/Log.h"
#include "varn/http/HttpTypes.h"

namespace varn::runtime {

using varn::lua::LuaEngine;

Runtime::Runtime(std::vector<std::string> args, std::size_t scriptArgIndex)
    : args_(std::move(args)),
      scriptArgIndex_(scriptArgIndex),
      workLedger_(std::make_shared<WorkLedger>()),
      mainLoop_(workLedger_),
      taskPool_(std::thread::hardware_concurrency(), workLedger_),
      lua_(std::make_unique<LuaEngine>(*this)) {
    // install the notify hook before the pool spins up any worker, so no worker can observe it unset.
    workLedger_->setNotify([this] { mainLoop_.wake(); });
    taskPool_.start();
}

Runtime::~Runtime() {
    stop();
}

int Runtime::runScript(const std::string& scriptPath) {
    return finishAfterUserChunk(lua_->runFile(scriptPath));
}

int Runtime::runString(const std::string& source, const std::string& chunkName) {
    return finishAfterUserChunk(lua_->runString(source, chunkName));
}

bool Runtime::runStringWithoutEventLoop(const std::string& source, const std::string& chunkName, std::string* errorMessage,
                                        void (*prePcallHook)(lua_State*, void*), void* prePcallUserdata) {
    return lua_->runStringWithoutEventLoop(source, chunkName, errorMessage, prePcallHook, prePcallUserdata);
}

int Runtime::finishAfterUserChunk(int loadRunExitCode) {
    if (loadRunExitCode != 0) {
        return loadRunExitCode;
    }

    // an async entry may have already failed or completed synchronously before the loop starts.
    if (unhandledError_) {
        return 1;
    }
    if (entryRequestedStop_) {
        return 0;
    }

    mainLoop_.setIdleExitPredicate([this] {
        return servers_.empty() && backgroundDrivers_.load(std::memory_order_acquire) == 0
            && workLedger_->depth() == 0;
    });
    mainLoop_.run();
    mainLoop_.setIdleExitPredicate({});
    return unhandledError_ ? 1 : 0;
}

void Runtime::onAsyncComplete(bool ok, bool stopLoopOnSuccess, const std::string& error) {
    if (!ok) {
        unhandledError_ = true;
        log::Log::error("Runtime", error.empty() ? "A task failed without a message." : error);
        mainLoop_.stop();
        return;
    }

    if (stopLoopOnSuccess) {
        entryRequestedStop_ = true;
        mainLoop_.stop();
    }
}

EventLoop& Runtime::mainLoop() {
    return mainLoop_;
}

TaskPool& Runtime::taskPool() {
    return taskPool_;
}

lua_State* Runtime::luaState() {
    return lua_->state();
}

void Runtime::addServer(std::shared_ptr<varn::http::HttpServer> server) {
    servers_.push_back(std::move(server));
    mainLoop_.wake();
}

void Runtime::retainBackgroundDriver() {
    backgroundDrivers_.fetch_add(1, std::memory_order_relaxed);
}

void Runtime::releaseBackgroundDriver() {
    backgroundDrivers_.fetch_sub(1, std::memory_order_acq_rel);
    mainLoop_.wake();
}

void Runtime::stop() {
    bool expected = false;
    if (!stopped_.compare_exchange_strong(expected, true)) {
        return;
    }

    for (auto& server : servers_) {
        if (server) {
            server->stop();
        }
    }

    taskPool_.stop();
    mainLoop_.stop();
    // workers are joined and the loop is stopped; drop any job still queued so its captured state
    // (e.g. AppState holding lua registry refs) is released now, while the lua_State is still alive,
    // rather than during member teardown after lua_close.
    mainLoop_.clearPendingJobs();
}

const std::vector<std::string>& Runtime::args() const {
    return args_;
}

} // namespace varn::runtime
