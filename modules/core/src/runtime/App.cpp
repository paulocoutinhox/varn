#include "varn/runtime/App.h"
#include "varn/log/Log.h"
#include "varn/runtime/Runtime.h"

#include <algorithm>
#include <cstdlib>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#if !defined(_WIN32) && !defined(VARN_NO_FORK)
#include <cerrno>
#include <csignal>
#include <sys/wait.h>
#include <unistd.h>
#endif

#if defined(_WIN32) && !defined(VARN_NO_FORK)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace varn::runtime
{

#if !defined(_WIN32) && !defined(VARN_NO_FORK)
namespace
{
volatile sig_atomic_t gWorkerShutdown = 0;
void onWorkerSignal(int)
{
    gWorkerShutdown = 1;
}

void resetChildSignals()
{
    // restore default signal handlers so a worker terminates on the parent's shutdown signal
    signal(SIGINT, SIG_DFL);
    signal(SIGTERM, SIG_DFL);
}
} // namespace
#endif

#if defined(_WIN32) && !defined(VARN_NO_FORK)
namespace
{
volatile LONG gWinWorkerShutdown = 0;

BOOL WINAPI onWorkerConsoleEvent(DWORD)
{
    // any console control event asks the supervisor to stop restarting and tear the workers down
    InterlockedExchange(&gWinWorkerShutdown, 1);
    return TRUE;
}

HANDLE spawnWorker(const std::wstring& commandLine)
{
    STARTUPINFOW startup{};
    startup.cb = static_cast<DWORD>(sizeof(startup));
    PROCESS_INFORMATION process{};

    std::vector<wchar_t> mutableCommand(commandLine.begin(), commandLine.end());
    mutableCommand.push_back(L'\0');

    if (!CreateProcessW(nullptr, mutableCommand.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup, &process))
    {
        return nullptr;
    }

    CloseHandle(process.hThread);
    return process.hProcess;
}
} // namespace
#endif

bool App::isEvalFlag(std::string_view flag)
{
    return flag == "-e" || flag == "--eval";
}

int App::workerCount()
{
    const char* value = std::getenv("VARN_WORKERS");
    if (!value)
    {
        return 1;
    }

    return std::clamp(std::atoi(value), 1, 1024);
}

#if !defined(_WIN32) && !defined(VARN_NO_FORK)
int App::superviseWorkers(int count, const std::function<int()>& runChild)
{
    std::vector<pid_t> workers;
    workers.reserve(static_cast<std::size_t>(count));

    for (int i = 0; i < count; ++i)
    {
        const pid_t pid = fork();
        if (pid < 0)
        {
            log::Log::error("App", "Failed to spawn a worker process.");
            break;
        }

        if (pid == 0)
        {
            resetChildSignals();
            _exit(runChild());
        }

        workers.push_back(pid);
    }

    // install handlers without SA_RESTART so the blocking waitpid returns EINTR on a shutdown signal
    struct sigaction action = {};
    action.sa_handler = onWorkerSignal;
    sigemptyset(&action.sa_mask);
    action.sa_flags = 0;
    sigaction(SIGINT, &action, nullptr);
    sigaction(SIGTERM, &action, nullptr);

    // restart any worker that exits unexpectedly and stop on the first shutdown signal
    while (!gWorkerShutdown)
    {
        int status = 0;
        const pid_t dead = ::waitpid(-1, &status, 0);
        if (dead < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }

            break;
        }

        if (gWorkerShutdown)
        {
            break;
        }

        const auto slot = std::find(workers.begin(), workers.end(), dead);
        if (slot == workers.end())
        {
            continue;
        }

        const pid_t replacement = fork();
        if (replacement == 0)
        {
            resetChildSignals();
            _exit(runChild());
        }

        if (replacement > 0)
        {
            *slot = replacement;
        }
        else
        {
            log::Log::error("App", "Failed to restart a worker process.");
            workers.erase(slot);
        }
    }

    for (const pid_t pid : workers)
    {
        ::kill(pid, SIGTERM);
    }

    for (const pid_t pid : workers)
    {
        int status = 0;
        ::waitpid(pid, &status, 0);
    }

    return 0;
}
#endif

#if defined(_WIN32) && !defined(VARN_NO_FORK)
int App::superviseWorkers(int count, const std::function<int()>& runChild)
{
    // windows has no fork, so each worker is a relaunch of this executable flagged so it runs the script instead of supervising again
    SetConsoleCtrlHandler(&onWorkerConsoleEvent, TRUE);
    SetEnvironmentVariableW(L"VARN_WORKER_CHILD", L"1");

    const std::wstring commandLine = GetCommandLineW();

    std::vector<HANDLE> workers;
    workers.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i)
    {
        HANDLE worker = spawnWorker(commandLine);
        if (worker != nullptr)
        {
            workers.push_back(worker);
            continue;
        }

        log::Log::error("App", "Failed to spawn a worker process.");
    }

    // if not a single worker could be launched, fall back to serving in this process rather than exiting having done nothing
    if (workers.empty())
    {
        SetConsoleCtrlHandler(&onWorkerConsoleEvent, FALSE);
        return runChild();
    }

    // restart any worker that exits unexpectedly and stop on the first console control event
    while (InterlockedCompareExchange(&gWinWorkerShutdown, 0, 0) == 0 && !workers.empty())
    {
        Sleep(100);
        if (InterlockedCompareExchange(&gWinWorkerShutdown, 0, 0) != 0)
        {
            break;
        }

        for (HANDLE& worker : workers)
        {
            if (WaitForSingleObject(worker, 0) != WAIT_OBJECT_0)
            {
                continue;
            }

            CloseHandle(worker);
            HANDLE replacement = spawnWorker(commandLine);
            if (replacement == nullptr)
            {
                log::Log::error("App", "Failed to restart a worker process.");
            }

            worker = replacement;
        }

        workers.erase(std::remove(workers.begin(), workers.end(), static_cast<HANDLE>(nullptr)), workers.end());
    }

    for (HANDLE worker : workers)
    {
        TerminateProcess(worker, 0);
    }

    for (HANDLE worker : workers)
    {
        WaitForSingleObject(worker, INFINITE);
        CloseHandle(worker);
    }

    return 0;
}
#endif

bool App::isWorkerChild()
{
    const char* value = std::getenv("VARN_WORKER_CHILD");
    return value != nullptr && value[0] != '\0';
}

int App::run(int argc, char** argv)
{
    if (argc < 2)
    {
        log::Log::error("App", "A script file or an inline source string is required.");
        return 2;
    }

    const std::string_view first = argv[1];
    const bool eval = isEvalFlag(first);
    if (eval && argc < 3)
    {
        log::Log::error("App", "The inline evaluation option needs a Lua source string.");
        return 2;
    }

    std::vector<std::string> args;
    args.reserve(static_cast<std::size_t>(argc));
    for (int i = 0; i < argc; ++i)
    {
        args.emplace_back(argv[i]);
    }

    // set the chunk (script path or -e source) as arg[0] and arguments after it as arg[1..]
    const std::size_t scriptArgIndex = eval ? 2 : 1;
    const std::string chunk = eval ? argv[2] : argv[1];

    auto runChild = [args = std::move(args), scriptArgIndex, eval, chunk]() mutable -> int
    {
        Runtime runtime(std::move(args), scriptArgIndex);
        if (eval)
        {
            return runtime.runString(chunk.c_str(), "=(eval)");
        }

        return runtime.runScript(chunk.c_str());
    };

    // a relaunched worker child runs the script directly instead of supervising, which would otherwise recurse
    const int workers = workerCount();
    if (workers > 1 && !isWorkerChild())
    {
#if !defined(VARN_NO_FORK)
        return superviseWorkers(workers, runChild);
#else
        log::Log::line("App", "Worker processes are not supported in this build, running a single process.");
#endif
    }

    return runChild();
}

} // namespace varn::runtime
