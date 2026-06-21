#include "varn/fs/FsStorage.h"

#include <chrono>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

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

FsStat FsStorage::stat(const std::string& path)
{
    std::error_code ec;

    // use symlink_status so a symlink is reported as a symlink rather than its target.
    const std::filesystem::file_status linkStatus = std::filesystem::symlink_status(path, ec);
    if (ec || linkStatus.type() == std::filesystem::file_type::not_found)
    {
        throw std::runtime_error("[FsStorage] The path does not exist.");
    }

    const std::filesystem::file_status status = std::filesystem::status(path, ec);
    if (ec)
    {
        throw std::runtime_error("[FsStorage] " + ec.message() + ".");
    }

    FsStat result;
    result.isDir = std::filesystem::is_directory(status);
    result.isFile = std::filesystem::is_regular_file(status);
    result.isSymlink = std::filesystem::is_symlink(linkStatus);

    if (result.isFile)
    {
        result.size = std::filesystem::file_size(path, ec);
        if (ec)
        {
            result.size = 0;
        }
    }

    const auto fileTime = std::filesystem::last_write_time(path, ec);
    if (!ec)
    {
        // map the file clock onto the system clock so the result is epoch seconds, since not every standard library offers clock_cast.
        const auto systemTime = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
            fileTime - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now());
        result.mtime = static_cast<std::int64_t>(std::chrono::system_clock::to_time_t(systemTime));
    }

    return result;
}

std::vector<std::string> FsStorage::readdir(const std::string& path)
{
    std::error_code ec;
    std::filesystem::directory_iterator it(path, ec);
    if (ec)
    {
        throw std::runtime_error("[FsStorage] " + ec.message() + ".");
    }

    std::vector<std::string> names;
    for (const auto& entry : it)
    {
        names.push_back(entry.path().filename().string());
    }

    return names;
}

void FsStorage::rename(const std::string& from, const std::string& to)
{
    std::error_code ec;
    std::filesystem::rename(from, to, ec);
    if (ec)
    {
        throw std::runtime_error("[FsStorage] " + ec.message() + ".");
    }
}

void FsStorage::copy(const std::string& from, const std::string& to)
{
    std::error_code ec;
    std::filesystem::copy_file(from, to, std::filesystem::copy_options::overwrite_existing, ec);
    if (ec)
    {
        throw std::runtime_error("[FsStorage] " + ec.message() + ".");
    }
}

void FsStorage::append(const std::string& path, const std::string& data)
{
    std::filesystem::path p(path);
    if (p.has_parent_path())
    {
        std::filesystem::create_directories(p.parent_path());
    }

    std::ofstream file(path, std::ios::binary | std::ios::app);
    if (!file)
    {
        throw std::runtime_error("[FsStorage] The file could not be opened for appending.");
    }

    file.write(data.data(), static_cast<std::streamsize>(data.size()));
    file.flush();
    if (!file)
    {
        throw std::runtime_error("[FsStorage] The file could not be appended.");
    }
}

std::string FsStorage::mkdtemp(const std::string& prefix)
{
    std::filesystem::path base = prefix;
    if (base.has_parent_path())
    {
        std::filesystem::create_directories(base.parent_path());
    }

    std::random_device device;
    std::mt19937_64 generator(device());
    std::uniform_int_distribution<std::uint64_t> distribution;

    // probe random suffixes until create_directory wins the race for an unused name.
    for (int attempt = 0; attempt < 256; ++attempt)
    {
        const std::string suffix = std::to_string(distribution(generator));
        std::filesystem::path candidate = base.string() + suffix;

        std::error_code ec;
        if (std::filesystem::create_directory(candidate, ec) && !ec)
        {
            return candidate.string();
        }
    }

    throw std::runtime_error("[FsStorage] A unique temporary directory could not be created.");
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
