#include "varn/process/ProcessRunner.h"

#include <array>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <fcntl.h>
#include <poll.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;

namespace varn::process
{

namespace
{
// drain stdout and stderr together so a child that fills one pipe buffer while writing the other never deadlocks the parent
void drainPipes(int outFd, int errFd, std::string& outData, std::string& errData)
{
    std::array<char, 65536> chunk{};

    // clang-format off
    pollfd fds[2];
    fds[0].fd = outFd;
    fds[1].fd = errFd;

    while (fds[0].fd >= 0 || fds[1].fd >= 0)
    {
        fds[0].events = fds[0].fd >= 0 ? POLLIN : 0;
        fds[1].events = fds[1].fd >= 0 ? POLLIN : 0;
        fds[0].revents = 0;
        fds[1].revents = 0;

        if (::poll(fds, 2, -1) < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }

            break;
        }

        for (int i = 0; i < 2; ++i)
        {
            if (fds[i].fd < 0 || fds[i].revents == 0)
            {
                continue;
            }

            const ssize_t got = ::read(fds[i].fd, chunk.data(), chunk.size());
            if (got > 0)
            {
                (i == 0 ? outData : errData).append(chunk.data(), static_cast<std::size_t>(got));
                continue;
            }

            if (got < 0 && errno == EINTR)
            {
                continue;
            }

            // eof or a read error retires this pipe from the poll set
            fds[i].fd = -1;
        }
    }
    // clang-format on
}

void closeFd(int& fd)
{
    if (fd >= 0)
    {
        ::close(fd);
        fd = -1;
    }
}

void setCloexec(int fd)
{
    const int flags = ::fcntl(fd, F_GETFD);
    if (flags >= 0)
    {
        ::fcntl(fd, F_SETFD, flags | FD_CLOEXEC);
    }
}
} // namespace

bool ProcessRunner::available()
{
    return true;
}

ProcessResult ProcessRunner::exec(const std::string& command)
{
    int outPipe[2] = {-1, -1};
    int errPipe[2] = {-1, -1};

    // serialize pipe creation and fork so a concurrent spawn on another worker thread cannot inherit these descriptors
    static std::mutex spawnMutex;
    pid_t pid = -1;
    {
        std::lock_guard<std::mutex> lock(spawnMutex);

        if (::pipe(outPipe) != 0 || ::pipe(errPipe) != 0)
        {
            closeFd(outPipe[0]);
            closeFd(outPipe[1]);
            closeFd(errPipe[0]);
            closeFd(errPipe[1]);
            throw std::runtime_error("[ProcessRunner] A pipe could not be created.");
        }

        // mark every pipe end close-on-exec so no other exec'd process keeps a copy that would block our reads
        setCloexec(outPipe[0]);
        setCloexec(outPipe[1]);
        setCloexec(errPipe[0]);
        setCloexec(errPipe[1]);

        pid = ::fork();
        if (pid < 0)
        {
            closeFd(outPipe[0]);
            closeFd(outPipe[1]);
            closeFd(errPipe[0]);
            closeFd(errPipe[1]);
            throw std::runtime_error("[ProcessRunner] The process could not be forked.");
        }

        if (pid == 0)
        {
            // wire the write ends onto stdout and stderr, which clears their close-on-exec flag, then run the command through the shell
            ::dup2(outPipe[1], STDOUT_FILENO);
            ::dup2(errPipe[1], STDERR_FILENO);
            ::close(outPipe[0]);
            ::close(outPipe[1]);
            ::close(errPipe[0]);
            ::close(errPipe[1]);

#if defined(__ANDROID__)
            ::execl("/system/bin/sh", "sh", "-c", command.c_str(), static_cast<char*>(nullptr));
#else
            ::execl("/bin/sh", "sh", "-c", command.c_str(), static_cast<char*>(nullptr));
#endif
            ::_exit(127);
        }

        // close the write ends inside the lock so the reads see eof once the child exits and no later spawn inherits them
        closeFd(outPipe[1]);
        closeFd(errPipe[1]);
    }

    ProcessResult result;
    drainPipes(outPipe[0], errPipe[0], result.stdoutData, result.stderrData);
    closeFd(outPipe[0]);
    closeFd(errPipe[0]);

    int status = 0;
    while (::waitpid(pid, &status, 0) < 0 && errno == EINTR)
    {
    }

    if (WIFEXITED(status))
    {
        result.code = WEXITSTATUS(status);
    }
    else if (WIFSIGNALED(status))
    {
        // reports a signalled exit as 128 plus the signal number
        result.code = 128 + WTERMSIG(status);
    }

    return result;
}

std::optional<std::string> ProcessRunner::getenv(const std::string& name)
{
    const char* value = ::getenv(name.c_str());
    if (value == nullptr)
    {
        return std::nullopt;
    }

    return std::string(value);
}

std::vector<std::pair<std::string, std::string>> ProcessRunner::environment()
{
    std::vector<std::pair<std::string, std::string>> entries;

    for (char** env = ::environ; env != nullptr && *env != nullptr; ++env)
    {
        const std::string line = *env;
        const std::size_t equals = line.find('=');
        if (equals == std::string::npos)
        {
            continue;
        }

        entries.emplace_back(line.substr(0, equals), line.substr(equals + 1));
    }

    return entries;
}

std::string ProcessRunner::cwd()
{
    std::vector<char> buffer(4096);

    while (true)
    {
        if (::getcwd(buffer.data(), buffer.size()) != nullptr)
        {
            return std::string(buffer.data());
        }

        if (errno != ERANGE)
        {
            throw std::runtime_error("[ProcessRunner] The current working directory could not be read.");
        }

        buffer.resize(buffer.size() * 2);
    }
}

} // namespace varn::process
