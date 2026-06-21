#include "varn/process/ProcessRunner.h"

#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace varn::process
{

ProcessResult ProcessRunner::exec(const std::string& /*command*/)
{
    throw std::runtime_error("[ProcessRunner] The process module is not available in this build.");
}

std::vector<std::pair<std::string, std::string>> ProcessRunner::environ()
{
    return {};
}

std::string ProcessRunner::cwd()
{
    return "/";
}

} // namespace varn::process
