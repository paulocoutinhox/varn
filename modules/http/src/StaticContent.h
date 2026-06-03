#pragma once

#include "varn/http/HttpTypes.h"

#include <cstdint>
#include <filesystem>
#include <string>

namespace varn::http {

class StaticContent {
public:
    static bool isHiddenComponent(const std::string& name);
    static bool serveFile(const HttpRequest& request, HttpResponse& response, const std::filesystem::path& path);
    static void serveListing(HttpResponse& response, const std::filesystem::path& root, const std::filesystem::path& dir);

private:
    static std::string httpDate(std::filesystem::file_time_type fileTime);
    static std::string headerValue(const HttpRequest& request, const std::string& name);
    static std::string htmlEscape(const std::string& value);
    static std::string urlEncodePath(const std::string& value);
    static std::string makeEtag(std::uintmax_t size, long long writeTime);
    static bool parseByteRange(const std::string& header, std::uintmax_t total, std::uintmax_t& start, std::uintmax_t& end);
};

} // namespace varn::http
