#pragma once

#include <vector>

namespace Primitives {
// std::string_view readUnsigned(std::span<unsigned char> data, size_t &offset);

void writeUnsignedShort(const unsigned short num, std::vector<unsigned char> &data);

} // namespace Primitives
