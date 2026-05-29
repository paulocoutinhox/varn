#include "varn/varn.h"

#include "VarnVersion.h"
#include "varn/runtime/Runtime.h"

#include <string>
#include <vector>

namespace {

varn::runtime::Runtime* asRuntime(varn_runtime* handle) {
    return reinterpret_cast<varn::runtime::Runtime*>(handle);
}

} // namespace

extern "C" {

varn_runtime* varn_runtime_new(void) {
    try {
        auto* runtime = new varn::runtime::Runtime(std::vector<std::string>{"varn"});
        return reinterpret_cast<varn_runtime*>(runtime);
    } catch (...) {
        return nullptr;
    }
}

int varn_runtime_run_file(varn_runtime* runtime, const char* path) {
    if (!runtime || !path) {
        return 2;
    }
    try {
        return asRuntime(runtime)->runScript(path);
    } catch (...) {
        return 1;
    }
}

int varn_runtime_run_string(varn_runtime* runtime, const char* source, const char* chunk_name) {
    if (!runtime || !source) {
        return 2;
    }
    try {
        return asRuntime(runtime)->runString(source, chunk_name ? chunk_name : "=(embedded)");
    } catch (...) {
        return 1;
    }
}

void varn_runtime_stop(varn_runtime* runtime) {
    if (runtime) {
        asRuntime(runtime)->stop();
    }
}

void varn_runtime_free(varn_runtime* runtime) {
    delete asRuntime(runtime);
}

const char* varn_version(void) {
    return VARN_VERSION_STRING;
}

} // extern "C"
