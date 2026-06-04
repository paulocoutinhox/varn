#include "varn/http/Router.h"

#include <algorithm>
#include <cctype>

namespace varn::http {

std::string Router::urlEncodeSegment(const std::string& value) {
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    out.reserve(value.size());
    for (unsigned char c : value) {
        const bool unreserved = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                                c == '-' || c == '_' || c == '.' || c == '~';
        if (unreserved) {
            out += static_cast<char>(c);
            continue;
        }
        out += '%';
        out += hex[c >> 4];
        out += hex[c & 0x0F];
    }
    return out;
}

std::vector<std::string> Router::splitPath(const std::string& path) {
    std::vector<std::string> parts;
    std::string current;

    for (char c : path) {
        if (c != '/') {
            current += c;
            continue;
        }
        if (!current.empty()) {
            parts.push_back(current);
            current.clear();
        }
    }

    if (!current.empty()) {
        parts.push_back(current);
    }

    return parts;
}

std::string Router::toUpper(std::string value) {
    for (char& c : value) {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    return value;
}

std::regex Router::compileConstraint(const std::string& spec) {
    if (spec == "int") return std::regex("[0-9]+");
    if (spec == "alpha") return std::regex("[A-Za-z]+");
    if (spec == "alnum") return std::regex("[A-Za-z0-9]+");
    if (spec == "slug") return std::regex("[A-Za-z0-9_-]+");
    if (spec == "uuid") return std::regex("[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}");
    return std::regex(spec);
}

int Router::add(const std::string& method, const std::string& pattern) {
    Route route;
    route.method = toUpper(method);

    for (const std::string& token : splitPath(pattern)) {
        Segment segment;
        segment.isParam = token.front() == ':';
        segment.text = segment.isParam ? token.substr(1) : token;
        route.segments.push_back(std::move(segment));
    }

    routes_.push_back(std::move(route));
    return static_cast<int>(routes_.size()) - 1;
}

void Router::setConstraint(int routeId, const std::string& param, const std::string& constraint) {
    routes_.at(routeId).constraints[param] = compileConstraint(constraint);
}

void Router::setName(int routeId, const std::string& name) {
    const auto existing = namedRoutes_.find(name);
    if (existing != namedRoutes_.end()) {
        throw std::runtime_error("[Router] The route name '" + name + "' is already in use.");
    }

    routes_.at(routeId).name = name;
    namedRoutes_[name] = routeId;
}

bool Router::pathMatches(const Route& route, const std::vector<std::string>& parts, std::vector<RouteParam>& outParams) const {
    if (route.segments.size() != parts.size()) {
        return false;
    }

    for (std::size_t i = 0; i < parts.size(); ++i) {
        const Segment& segment = route.segments[i];

        if (!segment.isParam) {
            if (segment.text != parts[i]) {
                return false;
            }
            continue;
        }

        const auto constraint = route.constraints.find(segment.text);
        if (constraint != route.constraints.end()) {
            // cap the matched length so a crafted segment cannot trigger catastrophic backtracking.
            if (parts[i].size() > kMaxSegmentLength) {
                return false;
            }
            bool matched = false;
            try {
                matched = std::regex_match(parts[i], constraint->second);
            } catch (const std::exception&) {
                matched = false;
            }
            if (!matched) {
                return false;
            }
        }

        outParams.push_back(RouteParam{segment.text, parts[i]});
    }

    return true;
}

MatchResult Router::match(const std::string& method, const std::string& path) const {
    const std::string wanted = toUpper(method);

    MatchResult result;

    // bound the total path length before any matching so constraint regexes stay cheap.
    if (path.size() > kMaxPathLength) {
        result.status = MatchStatus::NotFound;
        return result;
    }

    // reject control characters that desync segment matching and leak into params, logs and headers.
    for (char c : path) {
        if (static_cast<unsigned char>(c) < 0x20 || c == 0x7f) {
            result.status = MatchStatus::NotFound;
            return result;
        }
    }

    const std::vector<std::string> parts = splitPath(path);
    std::vector<std::string> allowed;
    int headFallback = -1;
    std::vector<RouteParam> headParams;

    for (std::size_t i = 0; i < routes_.size(); ++i) {
        const Route& route = routes_[i];

        std::vector<RouteParam> params;
        if (!pathMatches(route, parts, params)) {
            continue;
        }

        if (route.method == wanted || route.method == "ALL") {
            result.status = MatchStatus::Found;
            result.routeId = static_cast<int>(i);
            result.params = std::move(params);
            return result;
        }

        // a HEAD request falls back to the first matching GET handler, whose body the transport drops.
        if (wanted == "HEAD" && route.method == "GET" && headFallback < 0) {
            headFallback = static_cast<int>(i);
            headParams = params;
        }

        if (std::find(allowed.begin(), allowed.end(), route.method) == allowed.end()) {
            allowed.push_back(route.method);
        }
    }

    if (headFallback >= 0) {
        result.status = MatchStatus::Found;
        result.routeId = headFallback;
        result.params = std::move(headParams);
        return result;
    }

    if (!allowed.empty()) {
        result.status = MatchStatus::MethodNotAllowed;
        result.allowedMethods = std::move(allowed);
        return result;
    }

    result.status = MatchStatus::NotFound;
    return result;
}

std::optional<std::string> Router::buildUrl(const std::string& name,
                                            const std::unordered_map<std::string, std::string>& params) const {
    const auto entry = namedRoutes_.find(name);
    if (entry == namedRoutes_.end()) {
        return std::nullopt;
    }

    std::string url;
    for (const Segment& segment : routes_[entry->second].segments) {
        url += '/';

        if (!segment.isParam) {
            url += segment.text;
            continue;
        }

        const auto value = params.find(segment.text);
        if (value == params.end()) {
            return std::nullopt;
        }
        url += urlEncodeSegment(value->second);
    }

    return url.empty() ? std::string("/") : url;
}

} // namespace varn::http
