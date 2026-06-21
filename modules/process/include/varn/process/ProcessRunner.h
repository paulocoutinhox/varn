#pragma once

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

    static ProcessResult exec(const std::string& command);
    static std::vector<std::pair<std::string, std::string>> environ();
    static std::string cwd();
};

} // namespace varn::process
