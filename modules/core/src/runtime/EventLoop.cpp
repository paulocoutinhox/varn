#include "varn/runtime/EventLoop.h"

namespace varn::runtime {

EventLoop::EventLoop(std::shared_ptr<WorkLedger> ledger) : ledger(std::move(ledger)) {}

void EventLoop::post(Job job) {
    ledger->enter();
    {
        std::lock_guard<std::mutex> lock(mutex);
        jobs.push([ledger = ledger, j = std::move(job)]() mutable {
            j();
            ledger->leave();
        });
    }
#if !defined(__EMSCRIPTEN__)
    cv.notify_one();
#endif
}

void EventLoop::postDelayed(long long delayMs, Job job) {
    // a timer fires on the loop thread after the delay, so it never occupies a worker thread while it waits.
    ledger->enter();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(delayMs);
    {
        std::lock_guard<std::mutex> lock(mutex);
        timers.emplace(deadline, [ledger = ledger, j = std::move(job)]() mutable {
            j();
            ledger->leave();
        });
    }
#if !defined(__EMSCRIPTEN__)
    cv.notify_one();
#endif
}

void EventLoop::run() {
    // set the flag under the lock so a worker that called stop() between finishAfterUserChunk's flag check and this point cannot be clobbered by an out-of-order store.
    {
        std::lock_guard<std::mutex> lock(mutex);
        running = true;
    }

    while (running) {
        Job job;
        bool haveJob = false;

        {
            std::unique_lock<std::mutex> lock(mutex);
            // a pending timer keeps the loop waiting until its deadline (handled by wait_until's timeout), not via the predicate, so the wait does not spin while a timer is still in the future.
            const auto predicate = [&] {
                if (!running) {
                    return true;
                }
                if (!jobs.empty()) {
                    return true;
                }
                if (!timers.empty()) {
                    return false;
                }
                return static_cast<bool>(idleExitEligible) && idleExitEligible();
            };

            if (!timers.empty()) {
                cv.wait_until(lock, timers.begin()->first, predicate);
            } else {
                cv.wait(lock, predicate);
            }

            if (!running) {
                break;
            }

            // move every timer whose deadline has passed into the ready queue.
            const auto now = std::chrono::steady_clock::now();
            while (!timers.empty() && timers.begin()->first <= now) {
                jobs.push(std::move(timers.begin()->second));
                timers.erase(timers.begin());
            }

            if (jobs.empty()) {
                if (timers.empty() && idleExitEligible && idleExitEligible()) {
                    running = false;
                    break;
                }
                continue;
            }

            job = std::move(jobs.front());
            jobs.pop();
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
        std::lock_guard<std::mutex> lock(mutex);
        running = false;
    }
    cv.notify_all();
}

void EventLoop::clearPendingJobs() {
    // each queued job and timer entered the work ledger at post time, so drop them without invoking and release the matching ledger entries outside the lock so leave's notify path stays unlocked.
    std::queue<Job> drainedJobs;
    std::multimap<std::chrono::steady_clock::time_point, Job> drainedTimers;
    {
        std::lock_guard<std::mutex> lock(mutex);
        jobs.swap(drainedJobs);
        timers.swap(drainedTimers);
    }

    const std::size_t dropped = drainedJobs.size() + drainedTimers.size();
    for (std::size_t i = 0; i < dropped; ++i) {
        ledger->leave();
    }
}

void EventLoop::wake() {
    // take the lock so the notify cannot land in the gap between a waiter's predicate check and its wait.
    std::lock_guard<std::mutex> lock(mutex);
    cv.notify_all();
}

void EventLoop::setIdleExitPredicate(IdleExitPredicate predicate) {
    std::lock_guard<std::mutex> lock(mutex);
    idleExitEligible = std::move(predicate);
}

bool EventLoop::hasPendingJobs() const {
    std::lock_guard<std::mutex> lock(mutex);
    if (!jobs.empty()) {
        return true;
    }
    // a timer whose deadline has arrived is a job that is ready to run, so it counts as pending for any caller that only ever pumps via drainPostedJobs.
    return !timers.empty() && timers.begin()->first <= std::chrono::steady_clock::now();
}

bool EventLoop::hasPendingTimers() const {
    std::lock_guard<std::mutex> lock(mutex);
    return !timers.empty();
}

#if defined(__EMSCRIPTEN__)
void EventLoop::drainPostedJobs() {
    for (;;) {
        Job job;
        {
            std::lock_guard<std::mutex> lock(mutex);
            // move any timer whose deadline has passed into the ready queue, so a wasm pump that never calls run() still gets postDelayed jobs to fire eventually.
            const auto now = std::chrono::steady_clock::now();
            while (!timers.empty() && timers.begin()->first <= now) {
                jobs.push(std::move(timers.begin()->second));
                timers.erase(timers.begin());
            }
            if (jobs.empty()) {
                break;
            }
            job = std::move(jobs.front());
            jobs.pop();
        }
        if (job) {
            job();
        }
    }
}
#endif

} // namespace varn::runtime
