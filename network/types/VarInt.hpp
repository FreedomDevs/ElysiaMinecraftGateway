#pragma once

#include <span>
#include <vector>

namespace VarInt {

void writeVarInt(int value, std::vector<unsigned char> &buffer);

int readVarInt(std::span<unsigned char> buffer, size_t &index);

} // namespace VarInt
