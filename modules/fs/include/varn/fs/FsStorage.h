#pragma once

#include <string>

namespace varn::fs {

class FsStorage {
public:
    FsStorage() = delete;

    static std::string readAll(const std::string& path);
    static void writeAll(const std::string& path, const std::string& content);
    static bool exists(const std::string& path);
    static void mkdir(const std::string& path);
    static void removeRecursive(const std::string& path);
};

} // namespace varn::fs
