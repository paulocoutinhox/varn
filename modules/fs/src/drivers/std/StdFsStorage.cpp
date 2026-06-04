#include "varn/fs/FsStorage.h"

#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

namespace varn::fs {

std::string FsStorage::readAll(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("[FsStorage] The file could not be opened for reading.");
    }

    // read the whole file, like node's readFile — the caller owns the machine, so there is no
    // artificial size cap; a file too large for memory fails the allocation, which is the caller's call.
    std::string out;
    char chunk[65536];
    while (file) {
        file.read(chunk, sizeof(chunk));
        const std::streamsize got = file.gcount();
        if (got <= 0) {
            break;
        }
        out.append(chunk, static_cast<std::size_t>(got));
    }
    return out;
}

void FsStorage::writeAll(const std::string& path, const std::string& content) {
    std::filesystem::path p(path);
    if (p.has_parent_path()) {
        std::filesystem::create_directories(p.parent_path());
    }

    std::ofstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("[FsStorage] The file could not be opened for writing.");
    }

    file.write(content.data(), static_cast<std::streamsize>(content.size()));
    file.flush();
    if (!file) {
        throw std::runtime_error("[FsStorage] The file could not be written.");
    }
}

bool FsStorage::exists(const std::string& path) {
    return std::filesystem::exists(path);
}

} // namespace varn::fs
