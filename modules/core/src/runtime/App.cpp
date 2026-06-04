#include "varn/log/Log.h"
#include "varn/runtime/App.h"
#include "varn/runtime/Runtime.h"

#include <string>
#include <string_view>
#include <vector>

namespace varn::runtime {

bool App::isEvalFlag(std::string_view flag) {
    return flag == "-e" || flag == "--eval";
}

int App::run(int argc, char** argv) {
    if (argc < 2) {
        log::Log::error("App", "A script file or an inline source string is required.");
        return 2;
    }

    const std::string_view first = argv[1];
    const bool eval = isEvalFlag(first);
    if (eval && argc < 3) {
        log::Log::error("App", "The inline evaluation option needs a Lua source string.");
        return 2;
    }

    std::vector<std::string> args;
    args.reserve(static_cast<std::size_t>(argc));
    for (int i = 0; i < argc; ++i) {
        args.emplace_back(argv[i]);
    }

    // the chunk (script path, or the -e source) becomes arg[0]; arguments after it become arg[1..].
    const std::size_t scriptArgIndex = eval ? 2 : 1;
    Runtime runtime(std::move(args), scriptArgIndex);

    if (eval) {
        return runtime.runString(argv[2], "=(eval)");
    }

    return runtime.runScript(argv[1]);
}

} // namespace varn::runtime
