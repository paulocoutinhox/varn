#include "varn/process/ProcessRunner.h"

#include <array>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <sys/wait.h>
#include <unistd.h>

extern char** environ;

namespace varn::process
{

namespace
{
std::string drainPipe(int fd)
{
    std::string out;
    std::array<char, 65536> chunk{};

    // reads until eof
    while (true)
    {
        const ssize_t got = ::read(fd, chunk.data(), chunk.size());
        if (got > 0)
        {
            out.append(chunk.data(), static_cast<std::size_t>(got));
            continue;
        }

        if (got == 0)
        {
            break;
        }

        if (errno == EINTR)
        {
            continue;
        }

        break;
    }

    return out;
}

void closeFd(int& fd)
{
    if (fd >= 0)
    {
        ::close(fd);
        fd = -1;
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

    if (::pipe(outPipe) != 0 || ::pipe(errPipe) != 0)
    {
        closeFd(outPipe[0]);
        closeFd(outPipe[1]);
        closeFd(errPipe[0]);
        closeFd(errPipe[1]);
        throw std::runtime_error("[ProcessRunner] A pipe could not be created.");
    }

    const pid_t pid = ::fork();
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
        // wires the write ends onto stdout and stderr and runs the command through the shell
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

    // closes the write ends so the reads see eof once the child exits
    closeFd(outPipe[1]);
    closeFd(errPipe[1]);

    ProcessResult result;
    result.stdoutData = drainPipe(outPipe[0]);
    result.stderrData = drainPipe(errPipe[0]);
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
