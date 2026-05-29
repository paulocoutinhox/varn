#include "varn/xml/XmlSerializer.h"

#include <stdexcept>
#include <string>

namespace varn::xml {

std::string XmlSerializer::serialize(lua_State* /*L*/, int /*index*/) {
    throw std::runtime_error("[xml] The XML module is not available in this build.");
}

} // namespace varn::xml
