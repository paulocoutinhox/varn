#include "varn/crypto/CryptoPrimitives.h"

#include "varn/crypto/CryptoCodecs.h"

#include "Sha.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <stdexcept>

#include <sys/random.h>

namespace varn::crypto
{

namespace
{
std::string hmacOver(std::string_view algorithm, std::string_view key, std::string_view data)
{
    const std::size_t block = portable::Sha::blockSize(algorithm);

    std::string normalizedKey(key);
    if (normalizedKey.size() > block)
    {
        normalizedKey = portable::Sha::hashByName(algorithm, normalizedKey);
    }
    normalizedKey.resize(block, '\0');

    std::string innerKey(block, '\0');
    std::string outerKey(block, '\0');
    for (std::size_t i = 0; i < block; ++i)
    {
        const unsigned char k = static_cast<unsigned char>(normalizedKey[i]);
        innerKey[i] = static_cast<char>(k ^ 0x36);
        outerKey[i] = static_cast<char>(k ^ 0x5c);
    }

    const std::string inner = portable::Sha::hashByName(algorithm, innerKey + std::string(data));
    return portable::Sha::hashByName(algorithm, outerKey + inner);
}

[[noreturn]] void unavailable(const char* feature)
{
    throw std::runtime_error(std::string("[CryptoPrimitives] ") + feature + " is not available in this build.");
}
} // namespace

std::string CryptoPrimitives::digest(std::string_view algorithm, std::string_view data, bool outputHex)
{
    if (!portable::Sha::isSupported(algorithm))
    {
        throw std::runtime_error("[CryptoPrimitives] The requested hash algorithm is not available in this build.");
    }

    const std::string raw = portable::Sha::hashByName(algorithm, data);
    return outputHex ? CryptoCodecs::hexEncode(raw) : raw;
}

std::string CryptoPrimitives::hmac(std::string_view digestAlgorithm, std::string_view key, std::string_view data, bool outputHex)
{
    if (!portable::Sha::isSupported(digestAlgorithm))
    {
        throw std::runtime_error("[CryptoPrimitives] The requested hash algorithm is not available in this build.");
    }

    const std::string raw = hmacOver(digestAlgorithm, key, data);
    return outputHex ? CryptoCodecs::hexEncode(raw) : raw;
}

std::string CryptoPrimitives::randomBytes(std::size_t count)
{
    std::string out(count, '\0');

    std::size_t filled = 0;
    while (filled < count)
    {
        const std::size_t chunk = std::min<std::size_t>(256, count - filled);
        if (getentropy(out.data() + filled, chunk) != 0)
        {
            throw std::runtime_error("[CryptoPrimitives] The system random source failed.");
        }

        filled += chunk;
    }

    return out;
}

std::string CryptoPrimitives::base64Encode(std::string_view data, bool urlSafe, bool padding)
{
    return CryptoCodecs::base64Encode(data, urlSafe, padding);
}

std::string CryptoPrimitives::base64Decode(std::string_view data)
{
    return CryptoCodecs::base64Decode(data);
}

std::string CryptoPrimitives::hexEncode(std::string_view data)
{
    return CryptoCodecs::hexEncode(data);
}

std::string CryptoPrimitives::hexDecode(std::string_view data)
{
    return CryptoCodecs::hexDecode(data);
}

std::string CryptoPrimitives::uuidV4()
{
    const std::string random = randomBytes(16);

    unsigned char bytes[16];
    std::memcpy(bytes, random.data(), sizeof(bytes));

    // version 4 in the high nibble of byte 6 and the rfc 4122 variant in the high bits of byte 8
    bytes[6] = static_cast<unsigned char>((bytes[6] & 0x0F) | 0x40);
    bytes[8] = static_cast<unsigned char>((bytes[8] & 0x3F) | 0x80);

    return CryptoCodecs::formatUuid(bytes);
}

std::string CryptoPrimitives::uuidV7()
{
    const std::string random = randomBytes(10);

    unsigned char bytes[16];

    // the first 48 bits hold a big-endian unix millisecond timestamp so the ids sort by creation time
    const std::uint64_t millis = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count());
    bytes[0] = static_cast<unsigned char>((millis >> 40) & 0xFF);
    bytes[1] = static_cast<unsigned char>((millis >> 32) & 0xFF);
    bytes[2] = static_cast<unsigned char>((millis >> 24) & 0xFF);
    bytes[3] = static_cast<unsigned char>((millis >> 16) & 0xFF);
    bytes[4] = static_cast<unsigned char>((millis >> 8) & 0xFF);
    bytes[5] = static_cast<unsigned char>(millis & 0xFF);

    std::memcpy(bytes + 6, random.data(), 10);

    // version 7 in the high nibble of byte 6 and the rfc 4122 variant in the high bits of byte 8
    bytes[6] = static_cast<unsigned char>((bytes[6] & 0x0F) | 0x70);
    bytes[8] = static_cast<unsigned char>((bytes[8] & 0x3F) | 0x80);

    return CryptoCodecs::formatUuid(bytes);
}

std::string CryptoPrimitives::hashPassword(std::string_view /*password*/)
{
    unavailable("Password hashing");
}

bool CryptoPrimitives::verifyPassword(std::string_view /*password*/, std::string_view /*encoded*/)
{
    unavailable("Password hashing");
}

std::string CryptoPrimitives::aesGcmEncrypt(std::string_view /*key*/, std::string_view /*plaintext*/)
{
    unavailable("Authenticated encryption");
}

std::string CryptoPrimitives::aesGcmDecrypt(std::string_view /*key*/, std::string_view /*blob*/)
{
    unavailable("Authenticated encryption");
}

std::string CryptoPrimitives::pbkdf2(std::string_view /*password*/, std::string_view /*salt*/, std::size_t /*iterations*/, std::size_t /*keyLen*/, std::string_view /*algorithm*/)
{
    unavailable("PBKDF2");
}

std::string CryptoPrimitives::hkdf(std::string_view /*key*/, std::string_view /*salt*/, std::string_view /*info*/, std::size_t /*keyLen*/, std::string_view /*algorithm*/)
{
    unavailable("HKDF");
}

} // namespace varn::crypto
