#pragma once

#include <lua.hpp>
#include <string>

namespace varn::xml {

class XmlSerializer {
public:
    XmlSerializer() = delete;

    static std::string serialize(lua_State* L, int index);

    // node model: a table {name, attributes, children, text} round-trips between xml and lua.
    static std::string encodeNode(lua_State* L, int index, int indent);
    static bool parse(lua_State* L, const std::string& text);

    static std::string sanitizeElementName(const std::string& raw);
};

} // namespace varn::xml
