#pragma once

#include <filesystem>
#include <string_view>

namespace varn::zip {

// path-safety predicates for archive extraction, kept independent of libzip so they are unit-testable.
class ZipPath {
public:
    static bool entryPathSafe(std::string_view entry);
    static bool isSubpath(const std::filesystem::path& baseCanon, const std::filesystem::path& candidateCanon);
};

} // namespace varn::zip
