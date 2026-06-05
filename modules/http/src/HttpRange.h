#pragma once

#include <cstdint>
#include <string>

namespace varn::http {

// parses a single http byte range against a known total size.
class HttpRange {
public:
    HttpRange() = delete;

    // returns false for a malformed, unsatisfiable, or overflowing spec. an end past the file
    // is clamped to the last byte. only the single-range form is handled here.
    static bool parse(const std::string& header, std::uintmax_t total, std::uintmax_t& start, std::uintmax_t& end);

private:
    static bool allDigits(const std::string& value);
};

} // namespace varn::http
