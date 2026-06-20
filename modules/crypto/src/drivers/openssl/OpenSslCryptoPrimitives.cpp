#include "varn/crypto/CryptoPrimitives.h"

#include <climits>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#if defined(VARN_CRYPTO_DRIVER_OPENSSL) && VARN_CRYPTO_DRIVER_OPENSSL
#include <openssl/evp.h>
#include <openssl/opensslv.h>
#include <openssl/rand.h>
#if OPENSSL_VERSION_NUMBER >= 0x30000000L
#include <openssl/core_names.h>
#else
#include <openssl/hmac.h>
#endif
#endif

#if !defined(VARN_CRYPTO_DRIVER_OPENSSL) || !VARN_CRYPTO_DRIVER_OPENSSL
#error "OpenSslCryptoPrimitives.cpp is only built with VARN_CRYPTO_DRIVER_OPENSSL"
#endif

namespace varn::crypto
{

std::string CryptoPrimitives::trimAlgo(std::string_view in)
{
    while (!in.empty() && (in.front() == ' ' || in.front() == '\t'))
    {
        in.remove_prefix(1);
    }
    while (!in.empty() && (in.back() == ' ' || in.back() == '\t'))
    {
        in.remove_suffix(1);
    }

    return std::string(in);
}

std::string CryptoPrimitives::toHex(const unsigned char* data, std::size_t len)
{
    std::ostringstream out;
    out << std::hex << std::setfill('0');
    for (std::size_t i = 0; i < len; ++i)
    {
        out << std::setw(2) << static_cast<int>(data[i]);
    }

    return out.str();
}

std::string CryptoPrimitives::digest(std::string_view algorithm, std::string_view data, bool outputHex)
{
    const std::string algo = trimAlgo(algorithm);
    if (algo.empty())
    {
        throw std::runtime_error("[CryptoPrimitives] The hash algorithm name must not be empty.");
    }

    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (!ctx)
    {
        throw std::runtime_error("[CryptoPrimitives] The hash context could not be created.");
    }

    int initOk = 0;
#if OPENSSL_VERSION_NUMBER >= 0x30000000L
    EVP_MD* fetched = EVP_MD_fetch(nullptr, algo.c_str(), nullptr);
    if (!fetched)
    {
        EVP_MD_CTX_free(ctx);
        throw std::runtime_error("[CryptoPrimitives] The requested hash algorithm is not known.");
    }

    initOk = EVP_DigestInit_ex2(ctx, fetched, nullptr);
    if (initOk != 1)
    {
        EVP_MD_free(fetched);
        EVP_MD_CTX_free(ctx);
        throw std::runtime_error("[CryptoPrimitives] The hash could not be initialized.");
    }

    EVP_MD_free(fetched);
#else
    const EVP_MD* md = EVP_get_digestbyname(algo.c_str());
    if (!md)
    {
        EVP_MD_CTX_free(ctx);
        throw std::runtime_error("[CryptoPrimitives] The requested hash algorithm is not known.");
    }

    initOk = EVP_DigestInit_ex(ctx, md, nullptr);
    if (initOk != 1)
    {
        EVP_MD_CTX_free(ctx);
        throw std::runtime_error("[CryptoPrimitives] The hash could not be initialized.");
    }
#endif

    if (EVP_DigestUpdate(ctx, data.data(), data.size()) != 1)
    {
        EVP_MD_CTX_free(ctx);
        throw std::runtime_error("[CryptoPrimitives] The hash could not process the input.");
    }

    unsigned char buf[EVP_MAX_MD_SIZE];
    unsigned int outLen = 0;
    if (EVP_DigestFinal_ex(ctx, buf, &outLen) != 1)
    {
        EVP_MD_CTX_free(ctx);
        throw std::runtime_error("[CryptoPrimitives] The hash could not be finalized.");
    }

    EVP_MD_CTX_free(ctx);

    if (outputHex)
    {
        return toHex(buf, outLen);
    }

    return std::string(reinterpret_cast<const char*>(buf), static_cast<std::size_t>(outLen));
}

std::string CryptoPrimitives::hmac(std::string_view digestAlgorithm, std::string_view key, std::string_view data, bool outputHex)
{
    const std::string algo = trimAlgo(digestAlgorithm);
    if (algo.empty())
    {
        throw std::runtime_error("[CryptoPrimitives] The keyed hash algorithm name must not be empty.");
    }

#if OPENSSL_VERSION_NUMBER >= 0x30000000L
    EVP_MAC* mac = EVP_MAC_fetch(nullptr, "HMAC", nullptr);
    if (mac == nullptr)
    {
        throw std::runtime_error("[CryptoPrimitives] The keyed hash backend could not be loaded.");
    }

    EVP_MAC_CTX* ctx = EVP_MAC_CTX_new(mac);
    EVP_MAC_free(mac);
    if (ctx == nullptr)
    {
        throw std::runtime_error("[CryptoPrimitives] The keyed hash context could not be created.");
    }

    std::vector<char> digestName(algo.begin(), algo.end());
    digestName.push_back('\0');
    OSSL_PARAM params[] = {
        OSSL_PARAM_construct_utf8_string(OSSL_MAC_PARAM_DIGEST, digestName.data(), 0),
        OSSL_PARAM_construct_end()};

    if (EVP_MAC_init(ctx,
                     reinterpret_cast<const unsigned char*>(key.data()),
                     key.size(),
                     params) != 1)
    {
        EVP_MAC_CTX_free(ctx);
        throw std::runtime_error("[CryptoPrimitives] The keyed hash could not be initialized.");
    }

    if (EVP_MAC_update(ctx, reinterpret_cast<const unsigned char*>(data.data()), data.size()) != 1)
    {
        EVP_MAC_CTX_free(ctx);
        throw std::runtime_error("[CryptoPrimitives] The keyed hash could not process the input.");
    }

    const std::size_t macSize = EVP_MAC_CTX_get_mac_size(ctx);
    if (macSize == 0 || macSize > EVP_MAX_MD_SIZE)
    {
        EVP_MAC_CTX_free(ctx);
        throw std::runtime_error("[CryptoPrimitives] The keyed hash produced an invalid output size.");
    }

    unsigned char buf[EVP_MAX_MD_SIZE];
    std::size_t outLen = sizeof(buf);
    if (EVP_MAC_final(ctx, buf, &outLen, sizeof(buf)) != 1)
    {
        EVP_MAC_CTX_free(ctx);
        throw std::runtime_error("[CryptoPrimitives] The keyed hash could not be finalized.");
    }

    EVP_MAC_CTX_free(ctx);

    if (outputHex)
    {
        return toHex(buf, outLen);
    }

    return std::string(reinterpret_cast<const char*>(buf), outLen);
#else
    const EVP_MD* md = EVP_get_digestbyname(algo.c_str());
    if (md == nullptr)
    {
        throw std::runtime_error("[CryptoPrimitives] The requested keyed hash algorithm is not known.");
    }

    // the legacy hmac api takes int lengths, so an oversized input is rejected rather than silently truncated.
    if (key.size() > static_cast<std::size_t>(INT_MAX) || data.size() > static_cast<std::size_t>(INT_MAX))
    {
        throw std::runtime_error("[CryptoPrimitives] The keyed hash input is too large.");
    }

    const int keyLen = static_cast<int>(key.size());
    const int dataLen = static_cast<int>(data.size());
    unsigned char buf[EVP_MAX_MD_SIZE];
    unsigned int outLen = 0;
    if (HMAC(md,
             key.data(),
             keyLen,
             reinterpret_cast<const unsigned char*>(data.data()),
             dataLen,
             buf,
             &outLen) == nullptr)
    {
        throw std::runtime_error("[CryptoPrimitives] The keyed hash could not be computed.");
    }

    if (outputHex)
    {
        return toHex(buf, static_cast<std::size_t>(outLen));
    }

    return std::string(reinterpret_cast<const char*>(buf), static_cast<std::size_t>(outLen));
#endif
}

std::string CryptoPrimitives::randomBytes(std::size_t count)
{
    // the only hard bound is the OpenSSL api taking an int length, so the caller decides how much it wants.
    if (count > static_cast<std::size_t>(INT_MAX))
    {
        throw std::runtime_error("[CryptoPrimitives] The requested random byte count is too large.");
    }

    std::string bytes(count, '\0');
    if (RAND_bytes(reinterpret_cast<unsigned char*>(bytes.data()), static_cast<int>(count)) != 1)
    {
        throw std::runtime_error("[CryptoPrimitives] Random bytes could not be generated.");
    }

    return bytes;
}

} // namespace varn::crypto
