#include "varn/log/Log.h"

#include <iostream>
#include <string_view>

namespace varn::log {

namespace {

const char* levelTag(Level level) {
    switch (level) {
        case Level::Debug:
            return "DEBUG";
        case Level::Info:
            return "INFO";
        case Level::Warn:
            return "WARN";
        case Level::Error:
            return "ERROR";
        default:
            return "?";
    }
}

} // namespace

void Log::emit(Level level, std::string_view message) {
    std::cout << '[' << levelTag(level) << "] ";
    std::cout.write(message.data(), static_cast<std::streamsize>(message.size()));
    std::cout << '\n' << std::flush;
}

} // namespace varn::log
