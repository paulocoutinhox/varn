#include "varn/http/StaticFileHandler.h"
#include "varn/http/MimeTypes.h"

#include <cctype>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <sstream>
#include <string>

namespace varn::http {

namespace {

// formats a file modification time as an rfc 9110 http-date in gmt.
std::string httpDate(std::filesystem::file_time_type fileTime) {
    // map the file clock onto the system clock, since not every standard library offers clock_cast.
    const auto systemTime = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
        fileTime - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now());
    const std::time_t seconds = std::chrono::system_clock::to_time_t(systemTime);

    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &seconds);
#else
    gmtime_r(&seconds, &tm);
#endif

    char buffer[64];
    std::strftime(buffer, sizeof(buffer), "%a, %d %b %Y %H:%M:%S GMT", &tm);
    return std::string(buffer);
}

// a hidden path component must never be served, except the well-known directory used for protocols.
bool isHiddenComponent(const std::string& name) {
    return !name.empty() && name.front() == '.' && name != "." && name != ".." && name != ".well-known";
}

std::string headerValue(const HttpRequest& request, const std::string& name) {
    for (const auto& [key, value] : request.headers) {
        if (key.size() != name.size()) {
            continue;
        }
        bool same = true;
        for (std::size_t i = 0; i < key.size(); ++i) {
            if (std::tolower(static_cast<unsigned char>(key[i])) != std::tolower(static_cast<unsigned char>(name[i]))) {
                same = false;
                break;
            }
        }
        if (same) {
            return value;
        }
    }
    return "";
}

std::string htmlEscape(const std::string& value) {
    std::string out;
    out.reserve(value.size());
    for (char c : value) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            case '\'': out += "&#39;"; break;
            default: out += c;
        }
    }
    return out;
}

std::string urlEncodePath(const std::string& value) {
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    out.reserve(value.size());
    for (unsigned char c : value) {
        const bool unreserved = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                                c == '-' || c == '_' || c == '.' || c == '~' || c == '/';
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

std::string makeEtag(std::uintmax_t size, long long writeTime) {
    std::ostringstream tag;
    tag << '"' << std::hex << size << '-' << writeTime << '"';
    return tag.str();
}

bool parseByteRange(const std::string& header, std::uintmax_t total, std::uintmax_t& start, std::uintmax_t& end) {
    const std::string prefix = "bytes=";
    if (header.rfind(prefix, 0) != 0 || total == 0) {
        return false;
    }

    const std::string spec = header.substr(prefix.size());
    const std::size_t dash = spec.find('-');
    if (dash == std::string::npos) {
        return false;
    }

    const std::string first = spec.substr(0, dash);
    const std::string last = spec.substr(dash + 1);

    try {
        if (first.empty()) {
            // a suffix range asks for the final bytes of the file.
            if (last.empty()) {
                return false;
            }
            const std::uintmax_t count = std::stoull(last);
            if (count == 0) {
                return false;
            }
            start = count >= total ? 0 : total - count;
            end = total - 1;
            return true;
        }

        start = std::stoull(first);
        end = last.empty() ? total - 1 : std::stoull(last);
    } catch (const std::exception&) {
        return false;
    }

    if (end >= total) {
        end = total - 1;
    }
    return start <= end && start < total;
}

bool serveFile(const HttpRequest& request, HttpResponse& response, const std::filesystem::path& path) {
    std::error_code ec;
    const std::uintmax_t size = std::filesystem::file_size(path, ec);
    if (ec) {
        response.setStatus(500);
        response.end("Cannot read file");
        return true;
    }

    const auto fileTime = std::filesystem::last_write_time(path, ec);
    const long long writeTime = static_cast<long long>(fileTime.time_since_epoch().count());
    const std::string etag = makeEtag(size, writeTime);
    const std::string lastModified = httpDate(fileTime);

    response.setHeader("Cache-Control", "public, max-age=3600");
    response.setHeader("ETag", etag);
    response.setHeader("Last-Modified", lastModified);
    response.setHeader("Accept-Ranges", "bytes");
    // static responses bypass the middleware chain, so set the sniffing guard here.
    response.setHeader("X-Content-Type-Options", "nosniff");

    // a matching validator means the client already holds this exact file, so send no representation.
    // the entity tag takes precedence over the modification date per rfc 9110.
    if (headerValue(request, "If-None-Match") == etag ||
        (headerValue(request, "If-None-Match").empty() && headerValue(request, "If-Modified-Since") == lastModified)) {
        response.setStatus(304);
        response.end("");
        return true;
    }

    response.setHeader("Content-Type", MimeTypes::forPath(path.string()));

    // a head request advertises the headers and length without the transport reading any bytes.
    const bool headersOnly = request.method == "HEAD";

    // only single ranges are supported, so a multi-range request is ignored and the full body is sent.
    const std::string range = headerValue(request, "Range");
    if (!range.empty() && range.find(',') == std::string::npos) {
        std::uintmax_t start = 0;
        std::uintmax_t end = 0;
        if (!parseByteRange(range, size, start, end)) {
            response.setStatus(416);
            response.setHeader("Content-Range", "bytes */" + std::to_string(size));
            response.end("");
            return true;
        }

        response.setStatus(206);
        response.setHeader("Content-Range",
                           "bytes " + std::to_string(start) + "-" + std::to_string(end) + "/" + std::to_string(size));
        response.sendFile(path.string(), start, end - start + 1, headersOnly);
        return true;
    }

    response.setStatus(200);
    response.sendFile(path.string(), 0, size, headersOnly);
    return true;
}

void serveListing(HttpResponse& response, const std::filesystem::path& root, const std::filesystem::path& dir) {
    const std::string relative = std::filesystem::relative(dir, root).generic_string();
    const std::string title = relative.empty() || relative == "." ? "/" : "/" + relative;

    std::ostringstream html;
    html << "<!doctype html><html><head><meta charset=\"utf-8\"><title>Index of " << htmlEscape(title)
         << "</title></head><body><h1>Index of " << htmlEscape(title) << "</h1><ul>";

    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
        const std::string name = entry.path().filename().string();
        // keep hidden entries out of the listing for the same reason they are not served.
        if (isHiddenComponent(name)) {
            continue;
        }
        const std::string suffix = entry.is_directory(ec) ? "/" : "";
        // the visible label is html-escaped while the href is url-encoded to survive special chars.
        const std::string label = htmlEscape(name + suffix);
        const std::string href = htmlEscape(urlEncodePath(name + suffix));
        html << "<li><a href=\"" << href << "\">" << label << "</a></li>";
    }

    html << "</ul></body></html>";

    response.setStatus(200);
    response.setHeader("Content-Type", "text/html; charset=utf-8");
    response.setHeader("X-Content-Type-Options", "nosniff");
    response.end(html.str());
}

} // namespace

StaticFileHandler::StaticFileHandler(std::string publicDir, bool directoryListing)
    : publicDir_(std::move(publicDir)), directoryListing_(directoryListing) {}

bool StaticFileHandler::tryServe(const HttpRequest& request, HttpResponse& response) const {
    if (request.method != "GET" && request.method != "HEAD") {
        return false;
    }

    const std::string requestPath = request.path.empty() ? "/" : request.path;

    // reject control characters and embedded null bytes that could truncate the resolved path.
    for (char c : requestPath) {
        if (static_cast<unsigned char>(c) < 0x20 || c == 0x7f) {
            response.setStatus(400);
            response.end("Bad Request");
            return true;
        }
    }

    std::filesystem::path root = std::filesystem::weakly_canonical(publicDir_);
    std::filesystem::path candidate = std::filesystem::weakly_canonical(root / requestPath.substr(1));

    // keep the resolved path inside the public directory tree, rejecting siblings and traversal.
    const std::filesystem::path relative = candidate.lexically_relative(root);
    if (relative.empty() || *relative.begin() == "..") {
        response.setStatus(403);
        response.end("Forbidden");
        return true;
    }

    // hidden files such as .env or .git are never exposed, so treat them as absent.
    for (const auto& component : relative) {
        if (isHiddenComponent(component.string())) {
            return false;
        }
    }

    std::error_code ec;
    if (std::filesystem::is_directory(candidate, ec)) {
        std::filesystem::path index = candidate / "index.html";
        if (std::filesystem::is_regular_file(index, ec)) {
            candidate = index;
        } else if (directoryListing_) {
            serveListing(response, root, candidate);
            return true;
        } else {
            return false;
        }
    }

    if (!std::filesystem::is_regular_file(candidate, ec)) {
        return false;
    }

    return serveFile(request, response, candidate);
}

} // namespace varn::http
