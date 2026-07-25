#pragma once

#include <span>
#include <string>
#include <vector>

namespace String {
std::string readString(std::span<unsigned char> data, size_t &offset);

void writeString(const std::string &str, std::vector<unsigned char> &data);
} // namespace String
