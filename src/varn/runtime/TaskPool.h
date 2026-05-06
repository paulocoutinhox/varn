#pragma once

#include "varn/runtime/WorkLedger.h"

#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace varn {

class TaskPool {
public:
    using Job = std::function<void()>;

    explicit TaskPool(std::size_t threadCount, std::shared_ptr<WorkLedger> ledger);
    ~TaskPool();

    TaskPool(const TaskPool&) = delete;
    TaskPool& operator=(const TaskPool&) = delete;

    void post(Job job);
    void stop();

#if defined(__EMSCRIPTEN__)
    void drainPostedJobs();
    bool hasPostedJobs() const;
#endif

private:
    void workerLoop();

    std::shared_ptr<WorkLedger> ledger_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::queue<Job> jobs_;
    std::vector<std::thread> workers_;
    std::atomic<bool> running_{true};
};

} // namespace varn
