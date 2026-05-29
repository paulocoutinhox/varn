#include "varn/crypto/CryptoPrimitives.h"

#include <stdexcept>
#include <string>
#include <string_view>

#if !defined(VARN_CRYPTO_DRIVER_DUMMY) || !VARN_CRYPTO_DRIVER_DUMMY
#error "DummyCryptoPrimitives.cpp is only built with VARN_CRYPTO_DRIVER_DUMMY"
#endif

namespace varn::crypto {

namespace {
[[noreturn]] void disabled(std::string_view what) {
    throw std::runtime_error(
        std::string("crypto primitives are unavailable (VARN_CRYPTO_DRIVER=") + VARN_CRYPTO_DRIVER_NAME + "): " + std::string(what)
    );
}
} // namespace

std::string digest(std::string_view /*algorithm*/, std::string_view /*data*/, bool /*outputHex*/) {
    disabled("digest");
}

std::string hmac(std::string_view /*digestAlgorithm*/, std::string_view /*key*/, std::string_view /*data*/, bool /*outputHex*/) {
    disabled("hmac");
}

std::string randomBytes(std::size_t /*count*/) {
    disabled("randomBytes");
}

} // namespace varn::crypto
