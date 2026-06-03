#include "varn/log/Log.h"

#include <spdlog/logger.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include <memory>
#include <mutex>
#include <string>
#include <string_view>

namespace varn::log {

class SpdlogBridge {
public:
    static void ensureLogger() {
        static std::once_flag once;
        std::call_once(once, [] {
            auto sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
            auto logger = std::make_shared<spdlog::logger>("varn", sink);
            logger->set_level(spdlog::level::trace);
            spdlog::set_default_logger(std::move(logger));
        });
    }

    static spdlog::level::level_enum toLevel(Level level) {
        switch (level) {
            case Level::Debug:
                return spdlog::level::debug;
            case Level::Info:
                return spdlog::level::info;
            case Level::Warn:
                return spdlog::level::warn;
            case Level::Error:
                return spdlog::level::err;
            default:
                return spdlog::level::info;
        }
    }
};

void Log::emit(Level level, std::string_view message) {
    SpdlogBridge::ensureLogger();
    spdlog::default_logger()->log(SpdlogBridge::toLevel(level), std::string(message));
}

} // namespace varn::log
