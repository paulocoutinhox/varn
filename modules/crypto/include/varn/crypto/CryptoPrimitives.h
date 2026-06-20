#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace varn::crypto
{

class CryptoPrimitives final
{
public:
    CryptoPrimitives() = delete;

    static std::string digest(std::string_view algorithm, std::string_view data, bool outputHex);

    static std::string hmac(std::string_view digestAlgorithm, std::string_view key, std::string_view data, bool outputHex);

    static std::string randomBytes(std::size_t count);

private:
    static std::string trimAlgo(std::string_view in);

    static std::string toHex(const unsigned char* data, std::size_t len);
};

} // namespace varn::crypto
