#pragma once

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace varn::process
{

struct ProcessResult
{
    std::string stdoutData;
    std::string stderrData;
    int code = 0;
};

class ProcessRunner
{
public:
    ProcessRunner() = delete;

    static bool available();
    static ProcessResult exec(const std::string& command);
    static std::optional<std::string> getenv(const std::string& name);
    static std::vector<std::pair<std::string, std::string>> environment();
    static std::string cwd();
};

} // namespace varn::process
