#include "varn/log/Log.h"

#include <spdlog/logger.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include <memory>
#include <mutex>
#include <string>
#include <string_view>

namespace varn::log {

namespace {

std::once_flag g_logger_once;

void ensureLogger() {
    std::call_once(g_logger_once, [] {
        auto sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        auto logger = std::make_shared<spdlog::logger>("varn", sink);
        logger->set_level(spdlog::level::trace);
        spdlog::set_default_logger(std::move(logger));
    });
}

spdlog::level::level_enum toSpd(Level level) {
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

} // namespace

void Log::emit(Level level, std::string_view message) {
    ensureLogger();
    spdlog::default_logger()->log(toSpd(level), std::string(message));
}

} // namespace varn::log
