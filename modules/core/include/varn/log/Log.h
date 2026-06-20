#pragma once

#include <string>
#include <string_view>

namespace varn::log
{

enum class Level
{
    Debug,
    Info,
    Warn,
    Error
};

class Log
{
public:
    Log() = delete;

    static void emit(Level level, std::string_view message);

    static void line(std::string_view module, std::string message);

    static void error(std::string_view module, std::string message);
};

} // namespace varn::log
