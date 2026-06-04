#include "varn/runtime/EventLoop.h"

namespace varn::runtime {

EventLoop::EventLoop(std::shared_ptr<WorkLedger> ledger) : ledger_(std::move(ledger)) {}

void EventLoop::post(Job job) {
    ledger_->enter();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        jobs_.push([ledger = ledger_, j = std::move(job)]() mutable {
            j();
            ledger->leave();
        });
    }
#if !defined(__EMSCRIPTEN__)
    cv_.notify_one();
#endif
}

void EventLoop::postDelayed(long long delayMs, Job job) {
    ledger_->enter();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(delayMs);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        timers_.emplace(deadline, [ledger = ledger_, j = std::move(job)]() mutable {
            j();
            ledger->leave();
        });
    }
#if !defined(__EMSCRIPTEN__)
    cv_.notify_one();
#endif
}

void EventLoop::run() {
    running_ = true;

    while (running_) {
        Job job;
        bool haveJob = false;

        {
            std::unique_lock<std::mutex> lock(mutex_);
            // a pending timer keeps the loop waiting until its deadline (handled by wait_until's timeout),
            // not via the predicate, so the wait does not spin while a timer is still in the future.
            const auto predicate = [&] {
                if (!running_) {
                    return true;
                }
                if (!jobs_.empty()) {
                    return true;
                }
                if (!timers_.empty()) {
                    return false;
                }
                return static_cast<bool>(idleExitEligible_) && idleExitEligible_();
            };

            if (!timers_.empty()) {
                cv_.wait_until(lock, timers_.begin()->first, predicate);
            } else {
                cv_.wait(lock, predicate);
            }

            if (!running_) {
                break;
            }

            // move every timer whose deadline has passed into the ready queue.
            const auto now = std::chrono::steady_clock::now();
            while (!timers_.empty() && timers_.begin()->first <= now) {
                jobs_.push(std::move(timers_.begin()->second));
                timers_.erase(timers_.begin());
            }

            if (jobs_.empty()) {
                if (timers_.empty() && idleExitEligible_ && idleExitEligible_()) {
                    running_ = false;
                    break;
                }
                continue;
            }

            job = std::move(jobs_.front());
            jobs_.pop();
            haveJob = true;
        }

        if (haveJob && job) {
            job();
        }
    }
}

void EventLoop::stop() {
    // change the flag under the lock so a waiter cannot miss it between the predicate check and the wait.
    {
        std::lock_guard<std::mutex> lock(mutex_);
        running_ = false;
    }
    cv_.notify_all();
}

void EventLoop::clearPendingJobs() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::queue<Job> drained;
    jobs_.swap(drained);
    timers_.clear();
}

void EventLoop::wake() {
    // take the lock so the notify cannot land in the gap between a waiter's predicate check and its wait.
    std::lock_guard<std::mutex> lock(mutex_);
    cv_.notify_all();
}

void EventLoop::setIdleExitPredicate(IdleExitPredicate predicate) {
    std::lock_guard<std::mutex> lock(mutex_);
    idleExitEligible_ = std::move(predicate);
}

bool EventLoop::hasPendingJobs() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return !jobs_.empty();
}

#if defined(__EMSCRIPTEN__)
void EventLoop::drainPostedJobs() {
    for (;;) {
        Job job;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (jobs_.empty()) {
                break;
            }
            job = std::move(jobs_.front());
            jobs_.pop();
        }
        if (job) {
            job();
        }
    }
}
#endif

} // namespace varn::runtime
