#include "Primitives.hpp"
#include <cstdint>

namespace Primitives {
/*std::string_view readString(std::span<unsigned char> data, size_t &offset) {
  int stringLength = VarInt::readVarInt(data, offset);

  if (data.size() < offset + stringLength) {
    throw std::runtime_error("The string size is larger than the total data.");
  }

  std::string_view result((const char *)&*(data.begin() + offset), stringLength);

  offset += stringLength;

  return result;
}*/

void writeUnsignedShort(uint16_t num, std::vector<unsigned char> &data) {
  if constexpr (std::endian::native == std::endian::little)
    num = std::byteswap(num);

  const auto *bytes = reinterpret_cast<const uint8_t *>(&num);
  data.insert(data.end(), bytes, bytes + sizeof(num));
}
} // namespace Primitives
