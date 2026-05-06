#pragma once

#include <string>

namespace varn::fs::storage {

std::string readAll(const std::string& path);
void writeAll(const std::string& path, const std::string& content);
bool exists(const std::string& path);

} // namespace varn::fs::storage
