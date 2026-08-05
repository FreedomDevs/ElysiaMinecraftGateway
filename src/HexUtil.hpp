#pragma once
#include <iomanip>
#include <string>
#include <vector>

std::string to_hex(const unsigned char *data, size_t length);
std::string to_hex(std::span<unsigned char> data);
std::string to_hex(std::vector<unsigned char> data);
