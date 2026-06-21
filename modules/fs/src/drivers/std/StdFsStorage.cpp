#include "varn/fs/FsStorage.h"

#include <filesystem>
#include <fstream>
#include <mutex>
#include <stdexcept>
#include <string>

namespace varn::fs
{

std::string FsStorage::readAll(const std::string& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
    {
        throw std::runtime_error("[FsStorage] The file could not be opened for reading.");
    }

    // read the whole file into memory without an artificial size cap.
    std::string out;
    char chunk[65536];
    while (file)
    {
        file.read(chunk, sizeof(chunk));
        const std::streamsize got = file.gcount();
        if (got <= 0)
        {
            break;
        }

        out.append(chunk, static_cast<std::size_t>(got));
    }

    // a read fault leaves badbit set without eofbit, so a truncated result is reported instead of returned silently.
    if (file.bad())
    {
        throw std::runtime_error("[FsStorage] The file could not be read.");
    }

    return out;
}

void FsStorage::writeAll(const std::string& path, const std::string& content)
{
    std::filesystem::path p(path);
    if (p.has_parent_path())
    {
        std::filesystem::create_directories(p.parent_path());
    }

    std::ofstream file(path, std::ios::binary);
    if (!file)
    {
        throw std::runtime_error("[FsStorage] The file could not be opened for writing.");
    }

    file.write(content.data(), static_cast<std::streamsize>(content.size()));
    file.flush();
    if (!file)
    {
        throw std::runtime_error("[FsStorage] The file could not be written.");
    }
}

bool FsStorage::exists(const std::string& path)
{
    return std::filesystem::exists(path);
}

void FsStorage::mkdir(const std::string& path)
{
    std::error_code ec;
    std::filesystem::create_directories(path, ec);
    if (ec)
    {
        throw std::runtime_error("[FsStorage] " + ec.message() + ".");
    }
}

void FsStorage::removeRecursive(const std::string& path)
{
    std::error_code ec;
    std::filesystem::remove_all(path, ec);
    if (ec)
    {
        throw std::runtime_error("[FsStorage] " + ec.message() + ".");
    }
}

namespace
{
class StdFsHandle : public FsHandle
{
public:
    explicit StdFsHandle(std::fstream stream)
        : stream(std::move(stream))
    {
    }

    std::string read(std::size_t maxBytes) override
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (!stream.is_open())
        {
            throw std::runtime_error("[FsStorage] The file handle is closed.");
        }

        std::string buffer(maxBytes, '\0');
        stream.read(buffer.data(), static_cast<std::streamsize>(maxBytes));
        const std::streamsize got = stream.gcount();

        // a read fault leaves badbit set without eofbit, so a short read from a real error is reported instead of returned silently.
        if (stream.bad())
        {
            throw std::runtime_error("[FsStorage] The file handle could not be read.");
        }

        // clear eof and fail so the handle stays usable and a read past the end simply yields an empty string.
        stream.clear();
        buffer.resize(static_cast<std::size_t>(got));
        return buffer;
    }

    void write(const std::string& data) override
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (!stream.is_open())
        {
            throw std::runtime_error("[FsStorage] The file handle is closed.");
        }

        stream.write(data.data(), static_cast<std::streamsize>(data.size()));
        stream.flush();
        if (!stream)
        {
            throw std::runtime_error("[FsStorage] The file handle could not be written.");
        }
    }

    void close() override
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (stream.is_open())
        {
            stream.close();
        }
    }

private:
    std::fstream stream;
    std::mutex mutex;
};

std::ios::openmode openmodeFor(const std::string& mode)
{
    const std::ios::openmode binary = std::ios::binary;
    if (mode == "r")
    {
        return binary | std::ios::in;
    }
    if (mode == "w")
    {
        return binary | std::ios::out | std::ios::trunc;
    }
    if (mode == "a")
    {
        return binary | std::ios::out | std::ios::app;
    }
    if (mode == "r+")
    {
        return binary | std::ios::in | std::ios::out;
    }
    if (mode == "w+")
    {
        return binary | std::ios::in | std::ios::out | std::ios::trunc;
    }
    if (mode == "a+")
    {
        return binary | std::ios::in | std::ios::out | std::ios::app;
    }

    throw std::runtime_error("[FsStorage] The file mode is not one of r, w, a, r+, w+, a+.");
}
} // namespace

std::shared_ptr<FsHandle> FsStorage::open(const std::string& path, const std::string& mode)
{
    const std::ios::openmode flags = openmodeFor(mode);

    if (mode == "w" || mode == "a" || mode == "w+" || mode == "a+")
    {
        std::filesystem::path p(path);
        if (p.has_parent_path())
        {
            std::filesystem::create_directories(p.parent_path());
        }
    }

    std::fstream stream(path, flags);
    if (!stream.is_open())
    {
        throw std::runtime_error("[FsStorage] The file could not be opened.");
    }

    return std::make_shared<StdFsHandle>(std::move(stream));
}

} // namespace varn::fs
