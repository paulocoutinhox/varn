#include "varn/log/Log.h"

#include <sstream>

namespace varn::log {

void Log::line(std::string_view className, std::string_view methodName, std::string message) {
    std::ostringstream o;
    o << '[' << className << " : " << methodName << "] " << message;
    const std::string line = o.str();
    emit(Level::Info, line);
}

void Log::error(std::string_view className, std::string_view methodName, std::string message) {
    std::ostringstream o;
    o << '[' << className << " : " << methodName << "] " << message;
    const std::string line = o.str();
    emit(Level::Error, line);
}

} // namespace varn::log
