#include "varn/fs/FsStorage.h"

#include <stdexcept>
#include <string>

namespace varn::fs {

namespace {
[[noreturn]] void disabled() {
    throw std::runtime_error("[fs] The file system module is not available in this build.");
}
} // namespace

std::string FsStorage::readAll(const std::string& /*path*/) {
    disabled();
}

void FsStorage::writeAll(const std::string& /*path*/, const std::string& /*content*/) {
    disabled();
}

bool FsStorage::exists(const std::string& /*path*/) {
    return false;
}

} // namespace varn::fs
