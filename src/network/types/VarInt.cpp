#include "VarInt.hpp"
#include <cstdint>
#include <stdexcept>

namespace VarInt {
void writeVarInt(int value, std::vector<unsigned char> &buffer) {
  uint32_t uval = static_cast<uint32_t>(value);
  while (true) {
    unsigned char temp = static_cast<unsigned char>(uval & 0x7F);

    uval >>= 7;

    if (uval != 0)
      temp |= 0x80;

    buffer.push_back(temp);

    if (uval == 0)
      break;
  }
}

int readVarInt(std::span<unsigned char> buffer, size_t &index) {
  int value = 0;
  size_t position = 0;

  unsigned char currentByte;

  while (true) {
    if (index == buffer.size())
      throw std::runtime_error("VarInt exceeds buffer limit");

    currentByte = buffer[index++];

    value |= (currentByte & 0x7F) << (position++ * 7);

    if (position > 5)
      throw std::runtime_error("VarInt too long");

    if ((currentByte & 0x80) == 0)
      break;
  }

  return value;
}
} // namespace VarInt
