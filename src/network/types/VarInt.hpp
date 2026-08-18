#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace VarInt {

void writeVarInt(int value, std::vector<unsigned char> &buffer);

int readVarInt(std::span<unsigned char> buffer, size_t &index);

constexpr uint8_t varint_size(int value) {
  if (value < 0)
    return 5;
  if (value < (1 << 7))
    return 1;
  if (value < (1 << 14))
    return 2;
  if (value < (1 << 21))
    return 3;
  if (value < (1 << 28))
    return 4;
  return 5;
}

} // namespace VarInt
