#pragma once

#include <atomic>
#include <functional>

namespace varn::runtime {

// tracks in-flight async work so a runtime can decide when it is safe to shut down. enter and
// leave are called from arbitrary threads (worker pool, event loop, jni callbacks, fetch resumes),
// so the notify callback runs on whichever thread last touched the ledger. the callback MUST NOT
// take EventLoop::mutex_ or any other lock that enter/leave callers might already hold, since
// that would invert the lock order and could deadlock.
class WorkLedger {
public:
    void setNotify(std::function<void()> fn) { notify_ = std::move(fn); }

    void enter() {
        depth_.fetch_add(1, std::memory_order_relaxed);
        notifyIf();
    }

    void leave() {
        depth_.fetch_sub(1, std::memory_order_acq_rel);
        notifyIf();
    }

    int depth() const { return depth_.load(std::memory_order_acquire); }

private:
    void notifyIf() {
        if (notify_) {
            notify_();
        }
    }

    std::atomic<int> depth_{0};
    std::function<void()> notify_;
};

} // namespace varn::runtime
