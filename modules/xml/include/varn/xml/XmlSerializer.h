#pragma once

#include <lua.hpp>
#include <string>

namespace varn::xml {

class XmlSerializer {
public:
    XmlSerializer() = delete;

    static std::string serialize(lua_State* L, int index);

private:
    static std::string sanitizeElementName(const std::string& raw);
};

} // namespace varn::xml
