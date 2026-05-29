#include "varn/log/Log.h"

#include <sstream>

namespace varn::log {

void Log::line(std::string_view module, std::string message) {
    std::ostringstream o;
    o << '[' << module << "] " << message;
    const std::string line = o.str();
    emit(Level::Info, line);
}

void Log::error(std::string_view module, std::string message) {
    std::ostringstream o;
    o << '[' << module << "] " << message;
    const std::string line = o.str();
    emit(Level::Error, line);
}

} // namespace varn::log
