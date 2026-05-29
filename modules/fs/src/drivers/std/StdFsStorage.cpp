#include "varn/fs/FsStorage.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace varn::fs::storage {

std::string readAll(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("cannot open file for reading: " + path);
    }

    std::ostringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

void writeAll(const std::string& path, const std::string& content) {
    std::filesystem::path p(path);
    if (p.has_parent_path()) {
        std::filesystem::create_directories(p.parent_path());
    }

    std::ofstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("cannot open file for writing: " + path);
    }

    file.write(content.data(), static_cast<std::streamsize>(content.size()));
}

bool exists(const std::string& path) {
    return std::filesystem::exists(path);
}

} // namespace varn::fs::storage
