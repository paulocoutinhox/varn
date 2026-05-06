#pragma once

#include <string>
#include <string_view>

namespace varn::platform {

std::string osId();
std::string archId();

std::string libPrefix();
std::string shlibSuffix();

std::string libraryFilenameForName(std::string_view logicalName);

} // namespace varn::platform
