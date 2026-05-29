#include "varn/xml/XmlSerializer.h"

#include <stdexcept>
#include <string>

namespace varn::xml {

std::string serializeLuaTable(lua_State* /*L*/, int /*index*/) {
    throw std::runtime_error(
        std::string("XML serialization is unavailable in this build (VARN_XML_DRIVER=") + VARN_XML_DRIVER_NAME + ")."
    );
}

} // namespace varn::xml
