#include "varn/runtime/TaskPool.h"

namespace varn::runtime
{

TaskPool::TaskPool(std::size_t threadCount, std::shared_ptr<WorkLedger> ledger)
    : ledger(std::move(ledger))
    , threadCount(threadCount == 0 ? 4 : threadCount)
{
}

void TaskPool::start()
{
    // the workers must spawn only after the ledger notify hook is installed, so none observes a half-set callback.
#if !defined(__EMSCRIPTEN__)
    if (!workers.empty())
    {
        return;
    }
    workers.reserve(threadCount);
    for (std::size_t i = 0; i < threadCount; ++i)
    {
        workers.emplace_back([this]
                             { workerLoop(); });
    }
#endif
}

TaskPool::~TaskPool()
{
#if !defined(__EMSCRIPTEN__)
    stop();
#endif
}

void TaskPool::post(Job job)
{
    ledger->enter();
    {
        std::lock_guard<std::mutex> lock(mutex);
        jobs.push([ledger = ledger, j = std::move(job)]() mutable
                  {
            j();
            ledger->leave(); });
    }
#if !defined(__EMSCRIPTEN__)
    cv.notify_one();
#endif
}

#if defined(__EMSCRIPTEN__)
void TaskPool::drainPostedJobs()
{
    for (;;)
    {
        Job job;
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (jobs.empty())
            {
                break;
            }
            job = std::move(jobs.front());
            jobs.pop();
        }
        if (job)
        {
            job();
        }
    }
}

bool TaskPool::hasPostedJobs() const
{
    std::lock_guard<std::mutex> lock(mutex);
    return !jobs.empty();
}
#endif

void TaskPool::stop()
{
#if defined(__EMSCRIPTEN__)
    running = false;
#else
    {
        // the flag must change under the same mutex the workers wait on, otherwise a worker that has already evaluated the predicate but not yet blocked misses the notify and never wakes.
        std::lock_guard<std::mutex> lock(mutex);
        bool expected = true;
        if (!running.compare_exchange_strong(expected, false))
        {
            return;
        }
    }

    cv.notify_all();

    for (auto& worker : workers)
    {
        if (worker.joinable())
        {
            worker.join();
        }
    }
#endif
}

void TaskPool::workerLoop()
{
    while (running)
    {
        Job job;

        {
            std::unique_lock<std::mutex> lock(mutex);
            cv.wait(lock, [&]
                    { return !running || !jobs.empty(); });

            if (!running && jobs.empty())
            {
                break;
            }

            job = std::move(jobs.front());
            jobs.pop();
        }

        if (job)
        {
            job();
        }
    }
}

} // namespace varn::runtime
