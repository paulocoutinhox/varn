#include "varn/log/Log.h"

#include <iostream>
#include <mutex>
#include <string_view>

namespace varn::log
{

class StdoutBridge
{
public:
    static const char* levelTag(Level level)
    {
        switch (level)
        {
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
};

void Log::emit(Level level, std::string_view message)
{
    // worker threads and the loop thread can log concurrently, so serialize to keep lines from interleaving.
    static std::mutex sink;
    std::lock_guard<std::mutex> lock(sink);
    std::cout << '[' << StdoutBridge::levelTag(level) << "] ";
    std::cout.write(message.data(), static_cast<std::streamsize>(message.size()));
    std::cout << '\n'
              << std::flush;
}

} // namespace varn::log
