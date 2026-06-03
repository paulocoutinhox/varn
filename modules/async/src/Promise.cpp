#include "varn/async/Promise.h"
#include "varn/log/Log.h"
#include "varn/runtime/Runtime.h"

#include <string>

namespace varn::async {

using varn::runtime::Runtime;

constexpr const char* PromiseMeta = "varn.Promise";

struct PromiseUserdata {
    std::shared_ptr<Promise> promise;
};

int Promise::luaGc(lua_State* L) {
    auto* userdata = static_cast<PromiseUserdata*>(luaL_checkudata(L, 1, PromiseMeta));
    userdata->promise.~shared_ptr<Promise>();
    return 0;
}

int Promise::luaAwait(lua_State* L) {
    auto* promise = Promise::check(L, 1);
    const int outcome = promise->prepareAwait(L);
    if (outcome == -1) {
        return lua_yield(L, 0);
    }
    return outcome;
}

int Promise::luaIsDone(lua_State* L) {
    auto* promise = Promise::check(L, 1);
    lua_pushboolean(L, promise->state() != Promise::State::Pending);
    return 1;
}

Promise::Promise(Runtime& runtime) : runtime_(runtime) {}

Promise::~Promise() = default;

void Promise::resolve(std::string value) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (state_ != State::Pending) {
            return;
        }
        state_ = State::Resolved;
        customResolved_ = false;
        customPush_ = nullptr;
        value_ = std::move(value);
    }

    resumeWaitersOnMainLoop();
}

void Promise::resolveCustom(std::function<void(lua_State* L)> pushResolved) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (state_ != State::Pending) {
            return;
        }
        state_ = State::Resolved;
        customResolved_ = true;
        value_.clear();
        customPush_ = std::move(pushResolved);
    }

    resumeWaitersOnMainLoop();
}

void Promise::reject(std::string error) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (state_ != State::Pending) {
            return;
        }
        state_ = State::Rejected;
        customResolved_ = false;
        customPush_ = nullptr;
        error_ = std::move(error);
    }

    resumeWaitersOnMainLoop();
}

Promise::State Promise::state() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return state_;
}

int Promise::prepareAwait(lua_State* L) {
    lua_pushthread(L);
    const int threadRef = luaL_ref(L, LUA_REGISTRYINDEX);

    std::function<void(lua_State*)> custom;
    bool settledResolvedString = false;
    bool settledResolvedCustom = false;
    std::string snapshotValue;
    std::string snapshotError;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (state_ == State::Resolved) {
            luaL_unref(L, LUA_REGISTRYINDEX, threadRef);
            if (customResolved_) {
                custom = customPush_;
                settledResolvedCustom = true;
            } else {
                snapshotValue = value_;
                settledResolvedString = true;
            }
        } else if (state_ == State::Rejected) {
            luaL_unref(L, LUA_REGISTRYINDEX, threadRef);
            snapshotError = error_;
        } else {
            waitingRefs_.push_back(threadRef);
            return -1;
        }
    }

    if (settledResolvedString) {
        lua_pushlstring(L, snapshotValue.data(), snapshotValue.size());
        return 1;
    }
    if (settledResolvedCustom) {
        if (custom) {
            custom(L);
        }
        return 1;
    }
    lua_pushnil(L);
    lua_pushlstring(L, snapshotError.data(), snapshotError.size());
    return 2;
}

void Promise::runPendingResumes() {
    lua_State* mainState = runtime_.luaState();
    std::vector<int> refs;
    State state;
    std::string value;
    std::string error;

    std::function<void(lua_State*)> customPush;
    bool useCustom = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        refs.swap(waitingRefs_);
        state = state_;
        value = value_;
        error = error_;
        useCustom = customResolved_;
        customPush = customPush_;
    }

    for (int ref : refs) {
        lua_rawgeti(mainState, LUA_REGISTRYINDEX, ref);
        lua_State* coroutine = lua_tothread(mainState, -1);
        lua_pop(mainState, 1);
        if (coroutine == nullptr) {
            log::Log::error("async", "A pending task is no longer a valid coroutine.");
            luaL_unref(mainState, LUA_REGISTRYINDEX, ref);
            continue;
        }

        int argCount = 0;

        if (state == State::Resolved) {
            if (useCustom && customPush) {
                customPush(coroutine);
            } else {
                lua_pushlstring(coroutine, value.data(), value.size());
            }
            argCount = 1;
        } else {
            lua_pushnil(coroutine);
            lua_pushlstring(coroutine, error.data(), error.size());
            argCount = 2;
        }

#if defined(__EMSCRIPTEN__)
        lua_sethook(coroutine, nullptr, 0, 0);
#endif

        int nres = 0;
        int status = lua_resume(coroutine, mainState, argCount, &nres);
        if (status != LUA_OK && status != LUA_YIELD) {
            const char* message = lua_tostring(coroutine, -1);
            std::string detail = message ? message : "A task failed without a message.";
            log::Log::error("async", detail);
            if (nres > 0) {
                lua_pop(coroutine, nres);
            }
        }

        luaL_unref(mainState, LUA_REGISTRYINDEX, ref);
    }
}

void Promise::resumeWaitersOnMainLoop() {
    auto self = shared_from_this();
    runtime_.mainLoop().post([self] {
        self->runPendingResumes();
    });
}

Promise* Promise::check(lua_State* L, int index) {
    auto* userdata = static_cast<PromiseUserdata*>(luaL_checkudata(L, index, PromiseMeta));
    return userdata->promise.get();
}

void Promise::push(lua_State* L, std::shared_ptr<Promise> promise) {
    void* memory = lua_newuserdatauv(L, sizeof(PromiseUserdata), 0);
    new (memory) PromiseUserdata{std::move(promise)};

    luaL_getmetatable(L, PromiseMeta);
    lua_setmetatable(L, -2);
}

void Promise::installMetatable(lua_State* L) {
    if (luaL_newmetatable(L, PromiseMeta) == 0) {
        lua_pop(L, 1);
        return;
    }

    lua_pushcfunction(L, &Promise::luaGc);
    lua_setfield(L, -2, "__gc");

    lua_newtable(L);

    lua_pushcfunction(L, &Promise::luaAwait);
    lua_setfield(L, -2, "await");

    lua_pushcfunction(L, &Promise::luaIsDone);
    lua_setfield(L, -2, "isDone");

    lua_setfield(L, -2, "__index");
    lua_pop(L, 1);
}

} // namespace varn::async
