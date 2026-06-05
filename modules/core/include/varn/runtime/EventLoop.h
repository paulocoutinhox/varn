#pragma once

#include "varn/runtime/WorkLedger.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <queue>

namespace varn::runtime {

class EventLoop {
public:
    using Job = std::function<void()>;
    using IdleExitPredicate = std::function<bool()>;

    explicit EventLoop(std::shared_ptr<WorkLedger> ledger);

    void post(Job job);
    // schedules a job to run on the loop after delayMs, without occupying a worker thread.
    void postDelayed(long long delayMs, Job job);
    void run();
    void stop();
    void wake();

    // discards queued jobs and timers without running them; used at shutdown so captured state
    // (e.g. AppState) is released before the lua_State is destroyed, not after.
    void clearPendingJobs();

    void setIdleExitPredicate(IdleExitPredicate predicate);

    // reports the queue or a timer whose deadline has already arrived. an undue timer is reported by
    // hasPendingTimers, so a caller can decide whether to wait for it (instead of busy-polling).
    bool hasPendingJobs() const;
    bool hasPendingTimers() const;

#if defined(__EMSCRIPTEN__)
    void drainPostedJobs();
#endif

private:
    std::shared_ptr<WorkLedger> ledger_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::queue<Job> jobs_;
    std::multimap<std::chrono::steady_clock::time_point, Job> timers_;
    std::atomic<bool> running_{false};
    IdleExitPredicate idleExitEligible_;
};

} // namespace varn::runtime
