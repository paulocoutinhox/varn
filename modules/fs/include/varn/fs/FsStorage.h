#pragma once

#include <cstddef>
#include <memory>
#include <string>

namespace varn::fs
{

class FsHandle
{
public:
    virtual ~FsHandle() = default;

    // reads up to maxBytes from the current position, returning an empty string at end of file.
    virtual std::string read(std::size_t maxBytes) = 0;
    virtual void write(const std::string& data) = 0;
    virtual void close() = 0;
};

class FsStorage
{
public:
    FsStorage() = delete;

    static std::string readAll(const std::string& path);
    static void writeAll(const std::string& path, const std::string& content);
    static bool exists(const std::string& path);
    static void mkdir(const std::string& path);
    static void removeRecursive(const std::string& path);
    static std::shared_ptr<FsHandle> open(const std::string& path, const std::string& mode);
};

} // namespace varn::fs
