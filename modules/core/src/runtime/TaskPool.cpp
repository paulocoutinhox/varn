#include "varn/runtime/TaskPool.h"

namespace varn::runtime {

TaskPool::TaskPool(std::size_t threadCount, std::shared_ptr<WorkLedger> ledger)
    : ledger_(std::move(ledger)), threadCount_(threadCount == 0 ? 4 : threadCount) {}

void TaskPool::start() {
#if !defined(__EMSCRIPTEN__)
    if (!workers_.empty()) {
        return;
    }
    workers_.reserve(threadCount_);
    for (std::size_t i = 0; i < threadCount_; ++i) {
        workers_.emplace_back([this] {
            workerLoop();
        });
    }
#endif
}

TaskPool::~TaskPool() {
#if !defined(__EMSCRIPTEN__)
    stop();
#endif
}

void TaskPool::post(Job job) {
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

#if defined(__EMSCRIPTEN__)
void TaskPool::drainPostedJobs() {
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

bool TaskPool::hasPostedJobs() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return !jobs_.empty();
}
#endif

void TaskPool::stop() {
#if defined(__EMSCRIPTEN__)
    running_ = false;
#else
    {
        // the flag must change under the same mutex the workers wait on, otherwise a worker that
        // has already evaluated the predicate but not yet blocked misses the notify and never wakes.
        std::lock_guard<std::mutex> lock(mutex_);
        bool expected = true;
        if (!running_.compare_exchange_strong(expected, false)) {
            return;
        }
    }

    cv_.notify_all();

    for (auto& worker : workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }
#endif
}

void TaskPool::workerLoop() {
    while (running_) {
        Job job;

        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [&] {
                return !running_ || !jobs_.empty();
            });

            if (!running_ && jobs_.empty()) {
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

} // namespace varn::runtime
