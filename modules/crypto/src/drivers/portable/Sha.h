#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace varn::crypto::portable
{

std::string sha1(std::string_view data);
std::string sha224(std::string_view data);
std::string sha256(std::string_view data);
std::string sha384(std::string_view data);
std::string sha512(std::string_view data);

std::size_t blockSize(std::string_view algorithm);

bool isSupported(std::string_view algorithm);
std::string hashByName(std::string_view algorithm, std::string_view data);

} // namespace varn::crypto::portable
