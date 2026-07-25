#include "HexUtil.hpp"

std::string to_hex(const unsigned char *data, size_t length) {
  std::ostringstream oss;
  for (size_t i = 0; i < length; ++i) {
    if (i != 0)
      oss << " ";
    oss << std::uppercase << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(data[i]);
  }
  return oss.str();
}

std::string to_hex(std::vector<unsigned char> data) { return to_hex(data.data(), data.size()); }
