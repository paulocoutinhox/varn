#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace varn::crypto::portable
{

// each function returns the raw digest bytes for the given message.
std::string sha1(std::string_view data);
std::string sha224(std::string_view data);
std::string sha256(std::string_view data);
std::string sha384(std::string_view data);
std::string sha512(std::string_view data);

// block size in bytes for the hmac construction over each algorithm.
std::size_t blockSize(std::string_view algorithm);

// dispatches a normalized algorithm name to its hash, or returns an empty optional name on no match.
bool isSupported(std::string_view algorithm);
std::string hashByName(std::string_view algorithm, std::string_view data);

} // namespace varn::crypto::portable
