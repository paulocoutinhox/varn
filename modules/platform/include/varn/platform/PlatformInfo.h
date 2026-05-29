#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace varn::platform {

std::string osId();
std::string archId();

unsigned cpuCount();
std::size_t pointerSize();
std::string endianness();

std::string libPrefix();
std::string shlibSuffix();

std::string libraryFilenameForName(std::string_view logicalName);

} // namespace varn::platform
