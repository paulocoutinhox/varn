#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace varn::crypto::codecs
{

std::string base64Encode(std::string_view data, bool urlSafe, bool padding);

std::string base64Decode(std::string_view data);

std::string hexEncode(std::string_view data);

std::string hexDecode(std::string_view data);

std::string formatUuid(const unsigned char bytes[16]);

} // namespace varn::crypto::codecs
