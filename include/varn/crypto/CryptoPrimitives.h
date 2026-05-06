#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace varn::crypto {

std::string digest(std::string_view algorithm, std::string_view data, bool outputHex);

std::string hmac(std::string_view digestAlgorithm, std::string_view key, std::string_view data, bool outputHex);

std::string randomBytes(std::size_t count);

} // namespace varn::crypto
