#include "varn/log/Log.h"
#include "varn/runtime/App.h"
#include "varn/runtime/Runtime.h"

#include <string>
#include <string_view>
#include <vector>

namespace varn::runtime {

namespace {

bool isEvalFlag(std::string_view flag) {
    return flag == "-e" || flag == "--eval";
}

} // namespace

int App::run(int argc, char** argv) {
    if (argc < 2) {
        log::Log::error("varn", "A script file or an inline source string is required.");
        return 2;
    }

    std::vector<std::string> args;
    args.reserve(static_cast<std::size_t>(argc));
    for (int i = 0; i < argc; ++i) {
        args.emplace_back(argv[i]);
    }

    Runtime runtime(std::move(args));

    const std::string_view first = argv[1];
    if (isEvalFlag(first)) {
        if (argc < 3) {
            log::Log::error("varn", "The inline evaluation option needs a Lua source string.");
            return 2;
        }
        return runtime.runString(argv[2], "=(eval)");
    }

    return runtime.runScript(argv[1]);
}

} // namespace varn::runtime
