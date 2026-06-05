#pragma once

#include <atomic>

namespace varn::wasm {

class WasmAsyncHost {
public:
    WasmAsyncHost() = delete;

    // counts the http client fetches still in flight, so the wasm pump can wait for them before exiting.
    static std::atomic<int>& fetchInflight() {
        static std::atomic<int> counter{0};
        return counter;
    }
};

} // namespace varn::wasm
