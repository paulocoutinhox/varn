#include "varn/xml/XmlSerializer.h"

#include <pugixml.hpp>

#include <cctype>
#include <lua.hpp>
#include <sstream>
#include <string>

namespace varn::xml {

namespace {

constexpr int kMaxNodeDepth = 256;

// converts a pugixml element into a lua node table {name, attributes?, children?, text?}.
void pushElement(lua_State* L, const pugi::xml_node& node, int depth) {
    if (depth >= kMaxNodeDepth || lua_checkstack(L, 8) == 0) {
        lua_pushnil(L);
        return;
    }

    lua_newtable(L);

    lua_pushstring(L, node.name());
    lua_setfield(L, -2, "name");

    pugi::xml_attribute attr = node.first_attribute();
    if (attr) {
        lua_newtable(L);
        for (; attr; attr = attr.next_attribute()) {
            lua_pushstring(L, attr.value());
            lua_setfield(L, -2, attr.name());
        }
        lua_setfield(L, -2, "attributes");
    }

    // gather direct text while collecting child elements into an ordered array.
    std::string text;
    int childCount = 0;
    lua_newtable(L);
    const int childArray = lua_gettop(L);
    for (pugi::xml_node child = node.first_child(); child; child = child.next_sibling()) {
        const pugi::xml_node_type type = child.type();
        if (type == pugi::node_pcdata || type == pugi::node_cdata) {
            text += child.value();
        } else if (type == pugi::node_element) {
            pushElement(L, child, depth + 1);
            lua_rawseti(L, childArray, ++childCount);
        }
    }

    if (childCount > 0) {
        lua_setfield(L, -2, "children");
    } else {
        lua_pop(L, 1);
    }

    if (!text.empty()) {
        lua_pushlstring(L, text.data(), text.size());
        lua_setfield(L, -2, "text");
    }
}

// builds an xml element under parent from a lua node table.
void buildElement(pugi::xml_node parent, lua_State* L, int nodeIndex, int depth) {
    if (depth >= kMaxNodeDepth || lua_checkstack(L, 8) == 0) {
        return;
    }
    nodeIndex = lua_absindex(L, nodeIndex);

    std::string name = "node";
    lua_getfield(L, nodeIndex, "name");
    if (lua_isstring(L, -1)) {
        name = lua_tostring(L, -1);
    }
    lua_pop(L, 1);

    pugi::xml_node el = parent.append_child(XmlSerializer::sanitizeElementName(name).c_str());

    lua_getfield(L, nodeIndex, "attributes");
    if (lua_istable(L, -1)) {
        const int attrTable = lua_gettop(L);
        lua_pushnil(L);
        while (lua_next(L, attrTable) != 0) {
            const char* keyRaw = luaL_tolstring(L, -2, nullptr);
            const std::string key = keyRaw ? keyRaw : "";
            lua_pop(L, 1);
            const char* value = lua_isstring(L, -1) ? lua_tostring(L, -1) : "";
            el.append_attribute(XmlSerializer::sanitizeElementName(key).c_str()) = value;
            lua_pop(L, 1);
        }
    }
    lua_pop(L, 1);

    lua_getfield(L, nodeIndex, "children");
    if (lua_istable(L, -1)) {
        const int childArray = lua_gettop(L);
        const int count = static_cast<int>(lua_rawlen(L, childArray));
        for (int i = 1; i <= count; ++i) {
            lua_rawgeti(L, childArray, i);
            if (lua_istable(L, -1)) {
                buildElement(el, L, lua_gettop(L), depth + 1);
            }
            lua_pop(L, 1);
        }
    }
    lua_pop(L, 1);

    lua_getfield(L, nodeIndex, "text");
    if (lua_isstring(L, -1)) {
        el.text().set(lua_tostring(L, -1));
    }
    lua_pop(L, 1);
}

} // namespace

std::string XmlSerializer::sanitizeElementName(const std::string& raw) {
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

std::string XmlSerializer::serialize(lua_State* L, int index) {
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
        // luaL_tolstring pushes a copy of the key, so drop it before touching the value.
        // the value stays at -1 and lua_next still sees the original key on the next iteration.
        const char* keyRaw = luaL_tolstring(L, -2, nullptr);
        const std::string key = keyRaw ? keyRaw : "";
        lua_pop(L, 1);

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

std::string XmlSerializer::encodeNode(lua_State* L, int index, int indent) {
    index = lua_absindex(L, index);

    pugi::xml_document doc;
    pugi::xml_node decl = doc.prepend_child(pugi::node_declaration);
    decl.append_attribute("version") = "1.0";
    decl.append_attribute("encoding") = "UTF-8";

    if (lua_istable(L, index)) {
        buildElement(doc, L, index, 0);
    }

    std::ostringstream out;
    if (indent > 0) {
        const std::string pad(static_cast<std::size_t>(indent), ' ');
        doc.save(out, pad.c_str(), pugi::format_indent);
    } else {
        doc.save(out, "", pugi::format_raw);
    }
    return out.str();
}

bool XmlSerializer::parse(lua_State* L, const std::string& text) {
    pugi::xml_document doc;
    // pugixml does not load external entities or expand DTD entities, so xxe and entity bombs do not apply.
    const pugi::xml_parse_result result = doc.load_buffer(text.data(), text.size(), pugi::parse_default);
    if (!result) {
        return false;
    }

    pugi::xml_node root = doc.first_child();
    while (root && root.type() != pugi::node_element) {
        root = root.next_sibling();
    }
    if (!root) {
        return false;
    }

    pushElement(L, root, 0);
    return true;
}

} // namespace varn::xml
