#include "varn/process/ProcessRunner.h"

#include <cstddef>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace varn::process
{

namespace
{
std::wstring toWide(const std::string& utf8)
{
    if (utf8.empty())
    {
        return std::wstring();
    }

    const int size = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    std::wstring wide(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), wide.data(), size);
    return wide;
}

std::string toUtf8(const wchar_t* wide, int wideLen)
{
    if (wideLen <= 0)
    {
        return std::string();
    }

    const int size = WideCharToMultiByte(CP_UTF8, 0, wide, wideLen, nullptr, 0, nullptr, nullptr);
    std::string utf8(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide, wideLen, utf8.data(), size, nullptr, nullptr);
    return utf8;
}

std::string drainPipe(HANDLE pipe)
{
    std::string out;
    std::vector<char> chunk(65536);

    // reads until the write ends are closed and the buffer is empty
    while (true)
    {
        DWORD got = 0;
        const BOOL ok = ReadFile(pipe, chunk.data(), static_cast<DWORD>(chunk.size()), &got, nullptr);
        if (!ok || got == 0)
        {
            break;
        }

        out.append(chunk.data(), static_cast<std::size_t>(got));
    }

    return out;
}

void closeHandle(HANDLE& handle)
{
    if (handle != nullptr && handle != INVALID_HANDLE_VALUE)
    {
        CloseHandle(handle);
        handle = nullptr;
    }
}
} // namespace

bool ProcessRunner::available()
{
    return true;
}

ProcessResult ProcessRunner::exec(const std::string& command)
{
    SECURITY_ATTRIBUTES inheritable{};
    inheritable.nLength = static_cast<DWORD>(sizeof(inheritable));
    inheritable.bInheritHandle = TRUE;
    inheritable.lpSecurityDescriptor = nullptr;

    HANDLE outRead = nullptr;
    HANDLE outWrite = nullptr;
    HANDLE errRead = nullptr;
    HANDLE errWrite = nullptr;

    if (!CreatePipe(&outRead, &outWrite, &inheritable, 0) || !CreatePipe(&errRead, &errWrite, &inheritable, 0))
    {
        closeHandle(outRead);
        closeHandle(outWrite);
        closeHandle(errRead);
        closeHandle(errWrite);
        throw std::runtime_error("[ProcessRunner] A pipe could not be created.");
    }

    // keeps the parent read ends out of child inheritance
    SetHandleInformation(outRead, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(errRead, HANDLE_FLAG_INHERIT, 0);

    const std::wstring commandLine = L"cmd.exe /c " + toWide(command);
    std::vector<wchar_t> mutableCommand(commandLine.begin(), commandLine.end());
    mutableCommand.push_back(L'\0');

    STARTUPINFOW startup{};
    startup.cb = static_cast<DWORD>(sizeof(startup));
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    startup.hStdOutput = outWrite;
    startup.hStdError = errWrite;

    PROCESS_INFORMATION process{};

    const BOOL started = CreateProcessW(nullptr, mutableCommand.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process);
    if (!started)
    {
        closeHandle(outRead);
        closeHandle(outWrite);
        closeHandle(errRead);
        closeHandle(errWrite);
        throw std::runtime_error("[ProcessRunner] The process could not be started.");
    }

    // closes the parent copies of the write ends so the reads terminate when the child exits
    closeHandle(outWrite);
    closeHandle(errWrite);

    ProcessResult result;
    result.stdoutData = drainPipe(outRead);
    result.stderrData = drainPipe(errRead);

    closeHandle(outRead);
    closeHandle(errRead);

    WaitForSingleObject(process.hProcess, INFINITE);

    DWORD code = 0;
    GetExitCodeProcess(process.hProcess, &code);
    result.code = static_cast<int>(code);

    closeHandle(process.hProcess);
    closeHandle(process.hThread);

    return result;
}

std::optional<std::string> ProcessRunner::getenv(const std::string& name)
{
    const std::wstring wideName = toWide(name);

    // resolves the environment name case-insensitively
    const DWORD needed = GetEnvironmentVariableW(wideName.c_str(), nullptr, 0);
    if (needed == 0)
    {
        return std::nullopt;
    }

    std::wstring buffer(needed, L'\0');
    const DWORD written = GetEnvironmentVariableW(wideName.c_str(), buffer.data(), needed);

    return toUtf8(buffer.data(), static_cast<int>(written));
}

std::vector<std::pair<std::string, std::string>> ProcessRunner::environment()
{
    std::vector<std::pair<std::string, std::string>> entries;

    wchar_t* block = GetEnvironmentStringsW();
    if (block == nullptr)
    {
        return entries;
    }

    for (wchar_t* cursor = block; *cursor != L'\0';)
    {
        const std::wstring entry = cursor;
        cursor += entry.size() + 1;

        // skips the drive pseudo-variables that have no usable name
        const std::size_t equals = entry.find(L'=');
        if (equals == std::wstring::npos || equals == 0)
        {
            continue;
        }

        const std::string key = toUtf8(entry.data(), static_cast<int>(equals));
        const std::string value = toUtf8(entry.data() + equals + 1, static_cast<int>(entry.size() - equals - 1));
        entries.emplace_back(key, value);
    }

    FreeEnvironmentStringsW(block);
    return entries;
}

std::string ProcessRunner::cwd()
{
    const DWORD needed = GetCurrentDirectoryW(0, nullptr);
    if (needed == 0)
    {
        return std::string();
    }

    std::wstring buffer(needed, L'\0');
    const DWORD written = GetCurrentDirectoryW(needed, buffer.data());

    return toUtf8(buffer.data(), static_cast<int>(written));
}

} // namespace varn::process
