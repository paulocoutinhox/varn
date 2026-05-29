#include "varn/json/JsonSerializer.h"

#include <stdexcept>
#include <string>

namespace varn::json {

std::string serializeLuaTable(lua_State* /*L*/, int /*index*/) {
    throw std::runtime_error(
        std::string("JSON serialization is unavailable in this build (VARN_JSON_DRIVER=") + VARN_JSON_DRIVER_NAME + ")."
    );
}

} // namespace varn::json
