#pragma once

#include "varn/runtime/WorkLedger.h"

#include <atomic>
#include <condition_variable>
#include <functional>
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
    void run();
    void stop();
    void wake();

    void setIdleExitPredicate(IdleExitPredicate predicate);

    bool hasPendingJobs() const;

#if defined(__EMSCRIPTEN__)
    void drainPostedJobs();
#endif

private:
    std::shared_ptr<WorkLedger> ledger_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::queue<Job> jobs_;
    std::atomic<bool> running_{false};
    IdleExitPredicate idleExitEligible_;
};

} // namespace varn::runtime
