#include "varn/log/Log.h"
#include "varn/runtime/App.h"
#include "varn/runtime/Runtime.h"

#include <algorithm>
#include <cstdlib>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#if !defined(_WIN32)
#include <cerrno>
#include <csignal>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace varn::runtime {

#if !defined(_WIN32)
namespace {
volatile sig_atomic_t gWorkerShutdown = 0;
void onWorkerSignal(int) {
    gWorkerShutdown = 1;
}
} // namespace
#endif

bool App::isEvalFlag(std::string_view flag) {
    return flag == "-e" || flag == "--eval";
}

int App::workerCount() {
    const char* value = std::getenv("VARN_WORKERS");
    if (!value) {
        return 1;
    }
    return std::clamp(std::atoi(value), 1, 1024);
}

#if !defined(_WIN32)
int App::superviseWorkers(int count, const std::function<int()>& runChild) {
    std::vector<pid_t> workers;
    workers.reserve(static_cast<std::size_t>(count));

    for (int i = 0; i < count; ++i) {
        const pid_t pid = fork();
        if (pid < 0) {
            log::Log::error("App", "Failed to spawn a worker process.");
            break;
        }
        if (pid == 0) {
            _exit(runChild());
        }
        workers.push_back(pid);
    }

    std::signal(SIGINT, onWorkerSignal);
    std::signal(SIGTERM, onWorkerSignal);

    // restart any worker that exits unexpectedly, and stop on the first shutdown signal.
    while (!gWorkerShutdown) {
        int status = 0;
        const pid_t dead = ::waitpid(-1, &status, 0);
        if (dead < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }
        if (gWorkerShutdown) {
            break;
        }

        const auto slot = std::find(workers.begin(), workers.end(), dead);
        if (slot == workers.end()) {
            continue;
        }
        const pid_t replacement = fork();
        if (replacement == 0) {
            _exit(runChild());
        }
        if (replacement > 0) {
            *slot = replacement;
        }
    }

    for (const pid_t pid : workers) {
        ::kill(pid, SIGTERM);
    }
    for (const pid_t pid : workers) {
        int status = 0;
        ::waitpid(pid, &status, 0);
    }
    return 0;
}
#endif

int App::run(int argc, char** argv) {
    if (argc < 2) {
        log::Log::error("App", "A script file or an inline source string is required.");
        return 2;
    }

    const std::string_view first = argv[1];
    const bool eval = isEvalFlag(first);
    if (eval && argc < 3) {
        log::Log::error("App", "The inline evaluation option needs a Lua source string.");
        return 2;
    }

    std::vector<std::string> args;
    args.reserve(static_cast<std::size_t>(argc));
    for (int i = 0; i < argc; ++i) {
        args.emplace_back(argv[i]);
    }

    // the chunk, either the script path or the -e source, becomes arg[0] and arguments after it arg[1..].
    const std::size_t scriptArgIndex = eval ? 2 : 1;
    const std::string chunk = eval ? argv[2] : argv[1];

    auto runChild = [args = std::move(args), scriptArgIndex, eval, chunk]() mutable -> int {
        Runtime runtime(std::move(args), scriptArgIndex);
        if (eval) {
            return runtime.runString(chunk.c_str(), "=(eval)");
        }
        return runtime.runScript(chunk.c_str());
    };

    const int workers = workerCount();
    if (workers > 1) {
#if !defined(_WIN32)
        return superviseWorkers(workers, runChild);
#else
        log::Log::line("App", "Worker processes require a POSIX platform, running a single process.");
#endif
    }
    return runChild();
}

} // namespace varn::runtime
