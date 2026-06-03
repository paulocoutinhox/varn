#pragma once

#include <string_view>

namespace varn::runtime {

class App {
public:
    int run(int argc, char** argv);

private:
    static bool isEvalFlag(std::string_view flag);
};

} // namespace varn::runtime
