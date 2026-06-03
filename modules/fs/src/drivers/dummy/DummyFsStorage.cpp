#include "varn/fs/FsStorage.h"

#include <stdexcept>
#include <string>

namespace varn::fs {

std::string FsStorage::readAll(const std::string& /*path*/) {
    throw std::runtime_error("[fs] The file system module is not available in this build.");
}

void FsStorage::writeAll(const std::string& /*path*/, const std::string& /*content*/) {
    throw std::runtime_error("[fs] The file system module is not available in this build.");
}

bool FsStorage::exists(const std::string& /*path*/) {
    return false;
}

} // namespace varn::fs
