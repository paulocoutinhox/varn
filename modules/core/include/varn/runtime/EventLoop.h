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
    void postDelayed(long long delayMs, Job job);
    void run();
    void stop();
    void wake();

    void clearPendingJobs();

    void setIdleExitPredicate(IdleExitPredicate predicate);

    bool hasPendingJobs() const;
    bool hasPendingTimers() const;

#if defined(__EMSCRIPTEN__)
    void drainPostedJobs();
#endif

private:
    std::shared_ptr<WorkLedger> ledger;
    mutable std::mutex mutex;
    std::condition_variable cv;
    std::queue<Job> jobs;
    std::multimap<std::chrono::steady_clock::time_point, Job> timers;
    std::atomic<bool> running{false};
    IdleExitPredicate idleExitEligible;
};

} // namespace varn::runtime
