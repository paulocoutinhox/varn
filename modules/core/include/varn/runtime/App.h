#pragma once

#include <functional>
#include <string_view>

namespace varn::runtime {

class App {
public:
    int run(int argc, char** argv);

private:
    static bool isEvalFlag(std::string_view flag);
    static int workerCount();
#if !defined(_WIN32)
    static int superviseWorkers(int count, const std::function<int()>& runChild);
#endif
};

} // namespace varn::runtime
