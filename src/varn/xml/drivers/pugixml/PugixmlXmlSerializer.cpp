#include "varn/xml/XmlSerializer.h"

#include <pugixml.hpp>

#include <cctype>
#include <lua.hpp>
#include <sstream>
#include <string>

namespace varn::xml {

namespace {

std::string sanitizeElementName(const std::string& raw) {
    std::string out;
    out.reserve(raw.size());
    for (char c : raw) {
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-' || c == '.') {
            out += c;
        } else {
            out += '_';
        }
    }
    if (out.empty()) {
        return "item";
    }
    if (std::isdigit(static_cast<unsigned char>(out[0]))) {
        out.insert(out.begin(), 'n');
    }
    return out;
}

} // namespace

std::string serializeLuaTable(lua_State* L, int index) {
    index = lua_absindex(L, index);
    pugi::xml_document doc;
    pugi::xml_node decl = doc.prepend_child(pugi::node_declaration);
    decl.append_attribute("version") = "1.0";
    decl.append_attribute("encoding") = "UTF-8";
    pugi::xml_node root = doc.append_child("root");

    if (!lua_istable(L, index)) {
        std::ostringstream o;
        doc.save(o, "  ", pugi::format_indent);
        return o.str();
    }

    lua_pushnil(L);
    while (lua_next(L, index) != 0) {
        const char* keyRaw = luaL_tolstring(L, -2, nullptr);
        const std::string key = keyRaw ? keyRaw : "";
        const std::string tag = sanitizeElementName(key);
        pugi::xml_node el = root.append_child(tag.c_str());

        if (lua_isstring(L, -1)) {
            size_t len = 0;
            const char* s = lua_tolstring(L, -1, &len);
            el.text().set(s, len);
        } else if (lua_isnumber(L, -1)) {
            std::ostringstream n;
            n << lua_tonumber(L, -1);
            el.append_child(pugi::node_pcdata).set_value(n.str().c_str());
        } else if (lua_isboolean(L, -1)) {
            el.append_child(pugi::node_pcdata).set_value(lua_toboolean(L, -1) ? "true" : "false");
        } else if (lua_isnil(L, -1)) {
            el.append_child(pugi::node_pcdata).set_value("null");
        } else {
            el.append_child(pugi::node_pcdata).set_value("[unsupported]");
        }

        lua_pop(L, 1);
    }

    std::ostringstream out;
    doc.save(out, "  ", pugi::format_indent);
    return out.str();
}

} // namespace varn::xml
