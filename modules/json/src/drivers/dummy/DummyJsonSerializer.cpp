#include "varn/json/JsonSerializer.h"

#include <stdexcept>
#include <string>

namespace varn::json {

std::string JsonSerializer::serialize(lua_State* /*L*/, int /*index*/) {
    throw std::runtime_error("[json] The JSON module is not available in this build.");
}

} // namespace varn::json
