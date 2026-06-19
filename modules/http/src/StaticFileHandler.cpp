#include "varn/http/StaticFileHandler.h"

#include "StaticContent.h"

#include <filesystem>
#include <string>
#include <system_error>

namespace varn::http {

StaticFileHandler::StaticFileHandler(std::string publicDir, bool directoryListing)
    : publicDir(std::move(publicDir)), directoryListing(directoryListing) {}

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

    std::filesystem::path root = std::filesystem::weakly_canonical(publicDir);
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
        if (StaticContent::isHiddenComponent(component.string())) {
            return false;
        }
    }

    std::error_code ec;
    if (std::filesystem::is_directory(candidate, ec)) {
        std::filesystem::path index = candidate / "index.html";
        if (std::filesystem::is_regular_file(index, ec)) {
            candidate = index;
        } else if (directoryListing) {
            StaticContent::serveListing(response, root, candidate);
            return true;
        } else {
            return false;
        }
    }

    if (!std::filesystem::is_regular_file(candidate, ec)) {
        return false;
    }

    return StaticContent::serveFile(request, response, candidate);
}

} // namespace varn::http
