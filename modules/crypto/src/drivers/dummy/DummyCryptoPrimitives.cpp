#include "varn/crypto/CryptoPrimitives.h"

#include <stdexcept>
#include <string>
#include <string_view>

#if !defined(VARN_CRYPTO_DRIVER_DUMMY) || !VARN_CRYPTO_DRIVER_DUMMY
#error "DummyCryptoPrimitives.cpp is only built with VARN_CRYPTO_DRIVER_DUMMY"
#endif

namespace varn::crypto {

namespace {
[[noreturn]] void disabled() {
    throw std::runtime_error("[crypto] The crypto module is not available in this build.");
}
} // namespace

std::string CryptoPrimitives::digest(std::string_view /*algorithm*/, std::string_view /*data*/, bool /*outputHex*/) {
    disabled();
}

std::string CryptoPrimitives::hmac(std::string_view /*digestAlgorithm*/, std::string_view /*key*/, std::string_view /*data*/, bool /*outputHex*/) {
    disabled();
}

std::string CryptoPrimitives::randomBytes(std::size_t /*count*/) {
    disabled();
}

} // namespace varn::crypto
