#include "HttpUrlForm.h"

#include <lua.hpp>

#include <string>

namespace varn::http {

std::string HttpUrlForm::urlDecode(const std::string& input) {
    auto hexValue = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };

    std::string out;
    out.reserve(input.size());

    for (std::size_t i = 0; i < input.size(); ++i) {
        const char c = input[i];

        if (c == '+') {
            out += ' ';
            continue;
        }

        if (c == '%' && i + 2 < input.size()) {
            const int hi = hexValue(input[i + 1]);
            const int lo = hexValue(input[i + 2]);
            if (hi >= 0 && lo >= 0) {
                out += static_cast<char>(hi * 16 + lo);
                i += 2;
                continue;
            }
        }

        out += c;
    }

    return out;
}

void HttpUrlForm::pushFormUrlEncoded(lua_State* L, const std::string& body) {
    lua_newtable(L);

    // bound the field count so a body full of separators cannot drive unbounded parse work.
    constexpr int maxFields = 10000;
    int fields = 0;

    std::size_t pos = 0;
    while (pos <= body.size()) {
        const std::size_t amp = body.find('&', pos);
        const std::string pair = body.substr(pos, amp == std::string::npos ? std::string::npos : amp - pos);

        if (!pair.empty()) {
            const std::size_t eq = pair.find('=');
            const std::string key = urlDecode(eq == std::string::npos ? pair : pair.substr(0, eq));
            const std::string value = eq == std::string::npos ? std::string() : urlDecode(pair.substr(eq + 1));
            lua_pushlstring(L, value.data(), value.size());
            lua_setfield(L, -2, key.c_str());
            if (++fields >= maxFields) {
                break;
            }
        }

        if (amp == std::string::npos) {
            break;
        }
        pos = amp + 1;
    }
}

} // namespace varn::http
