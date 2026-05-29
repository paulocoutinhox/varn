#pragma once

#include <atomic>

namespace varn::wasm {

inline std::atomic<int>& httpClientFetchInflight() {
    static std::atomic<int> counter{0};
    return counter;
}

} // namespace varn::wasm
