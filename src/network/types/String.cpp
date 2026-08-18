#include "String.hpp"
#include "VarInt.hpp"
#include <stdexcept>

namespace String {
std::string_view readString(std::span<unsigned char> data, size_t &offset) {
  int stringLength = VarInt::readVarInt(data, offset);

  if (data.size() < offset + stringLength) {
    throw std::runtime_error("The string size is larger than the total data.");
  }

  std::string_view result((const char *)&*(data.begin() + offset), stringLength);

  offset += stringLength;

  return result;
}

void writeString(const std::string_view str, std::vector<unsigned char> &data) {
  std::vector<unsigned char> lengthPrefix;
  VarInt::writeVarInt(str.size(), lengthPrefix);

  data.insert(data.end(), lengthPrefix.begin(), lengthPrefix.end());

  data.insert(data.end(), str.begin(), str.end());
}
} // namespace String
