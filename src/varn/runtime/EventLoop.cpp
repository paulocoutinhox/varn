#include "varn/runtime/EventLoop.h"

namespace varn {

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

void EventLoop::run() {
    running_ = true;

    while (running_) {
        Job job;
        bool haveJob = false;

        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [&] {
                if (!running_) {
                    return true;
                }
                if (!jobs_.empty()) {
                    return true;
                }
                return static_cast<bool>(idleExitEligible_) && idleExitEligible_();
            });

            if (!running_) {
                break;
            }

            if (jobs_.empty()) {
                if (idleExitEligible_ && idleExitEligible_()) {
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
    running_ = false;
    cv_.notify_all();
}

void EventLoop::wake() {
    cv_.notify_all();
}

void EventLoop::setIdleExitPredicate(IdleExitPredicate predicate) {
    std::lock_guard<std::mutex> lock(mutex_);
    idleExitEligible_ = std::move(predicate);
}

bool EventLoop::isRunning() const {
    return running_;
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

} // namespace varn
