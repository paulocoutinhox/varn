#include "varn/fs/FsStorage.h"

#include <stdexcept>
#include <string>

namespace varn::fs::storage {

namespace {
[[noreturn]] void disabled(const char* what) {
    throw std::runtime_error(
        std::string("fs storage driver is ") + VARN_FS_DRIVER_NAME + ": " + what
    );
}
} // namespace

std::string readAll(const std::string& /*path*/) {
    disabled("readAll");
}

void writeAll(const std::string& /*path*/, const std::string& /*content*/) {
    disabled("writeAll");
}

bool exists(const std::string& /*path*/) {
    return false;
}

} // namespace varn::fs::storage
