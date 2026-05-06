#pragma once

#include <atomic>
#include <functional>

namespace varn {

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

} // namespace varn
