#pragma once

#include <span>
#include <string_view>
#include <vector>

namespace String {
std::string_view readString(std::span<unsigned char> data, size_t &offset);

void writeString(const std::string_view str, std::vector<unsigned char> &data);
} // namespace String
