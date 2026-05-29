#include "varn/http/StaticFileHandler.h"
#include "varn/http/MimeTypes.h"

#include <filesystem>
#include <fstream>
#include <sstream>

namespace varn::http {

StaticFileHandler::StaticFileHandler(std::string publicDir) : publicDir_(std::move(publicDir)) {}

bool StaticFileHandler::tryServe(const HttpRequest& request, HttpResponse& response) const {
    if (request.method != "GET" && request.method != "HEAD") {
        return false;
    }

    std::filesystem::path root = std::filesystem::weakly_canonical(publicDir_);
    std::string safePath = request.path.empty() || request.path == "/" ? "/index.html" : request.path;

    // keep the canonical file path inside the public directory tree
    std::filesystem::path candidate = std::filesystem::weakly_canonical(root / safePath.substr(1));
    std::string rootString = root.string();
    std::string candidateString = candidate.string();

    if (candidateString.rfind(rootString, 0) != 0) {
        response.setStatus(403);
        response.end("forbidden");
        return true;
    }

    if (!std::filesystem::is_regular_file(candidate)) {
        return false;
    }

    std::ifstream file(candidate, std::ios::binary);
    if (!file) {
        response.setStatus(500);
        response.end("cannot read file");
        return true;
    }

    std::ostringstream buffer;
    buffer << file.rdbuf();

    response.setStatus(200);
    response.setHeader("Content-Type", mimeTypeForPath(candidate.string()));
    response.setHeader("Cache-Control", "public, max-age=3600");

    if (request.method == "HEAD") {
        response.end("");
    } else {
        response.end(buffer.str());
    }

    return true;
}

} // namespace varn::http
