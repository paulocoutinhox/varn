#include "varn/runtime/Runtime.h"
#include "varn/lua/LuaEngine.h"
#include "varn/http/HttpTypes.h"

namespace varn::runtime {

using varn::lua::LuaEngine;

Runtime::Runtime(std::vector<std::string> args)
    : args_(std::move(args)),
      workLedger_(std::make_shared<WorkLedger>()),
      mainLoop_(workLedger_),
      taskPool_(std::thread::hardware_concurrency(), workLedger_),
      lua_(std::make_unique<LuaEngine>(*this)) {
    workLedger_->setNotify([this] { mainLoop_.wake(); });
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

    mainLoop_.setIdleExitPredicate([this] {
        return servers_.empty() && backgroundDrivers_.load(std::memory_order_acquire) == 0
            && workLedger_->depth() == 0;
    });
    mainLoop_.run();
    mainLoop_.setIdleExitPredicate({});
    return 0;
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
}

const std::vector<std::string>& Runtime::args() const {
    return args_;
}

} // namespace varn::runtime
