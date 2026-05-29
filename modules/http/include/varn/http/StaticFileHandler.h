#pragma once

#include "varn/http/HttpTypes.h"

#include <string>

namespace varn::http {

class StaticFileHandler {
public:
    explicit StaticFileHandler(std::string publicDir);
    bool tryServe(const HttpRequest& request, HttpResponse& response) const;

private:
    std::string publicDir_;
};

} // namespace varn::http
