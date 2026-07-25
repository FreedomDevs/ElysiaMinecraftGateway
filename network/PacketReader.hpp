#pragma once

#include "types/String.hpp"
#include "types/VarInt.hpp"
#include <cmath>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>

class PacketReader {
private:
  std::span<unsigned char> data;
  size_t index;
  int packet_id;

public:
  PacketReader(std::span<unsigned char> data) : data(data), index(0), packet_id(-1) {}
  std::optional<size_t> readUncompressed() {
    size_t packet_size;
    try {
      packet_size = this->readVarInt();
    } catch (const std::runtime_error &e) {
      if (e.what() == std::string("VarInt exceeds buffer limit")) {
        return std::nullopt;
      }

      throw e;
    }

    if (packet_size > pow(2, 23)) {
      throw std::runtime_error("Packet size is too long.");
    }

    if (data.size() < index + packet_size) {
      return std::nullopt;
    }

    this->data = std::span<unsigned char>(data.data(), index + packet_size);
    this->packet_id = this->readVarInt();
    return data.size();
  }

  void readCompressed() {};

  int getPacketId() { return packet_id; }

  unsigned short readUnsignedShort() {
    if (index + 2 > data.size()) {
      throw std::runtime_error("Packet length is smaller than expected for unsigned short.");
    }

    unsigned short result = (data[index] << 8) | data[index + 1];
    index += 2;

    return result;
  }
  long readLong() {
    if (index + 8 > data.size()) {
      throw std::runtime_error("Packet length is smaller than expected for long.");
    }

    long result = (static_cast<long>(data[index]) << 56) | (static_cast<long>(data[index + 1]) << 48) |
                  (static_cast<long>(data[index + 2]) << 40) | (static_cast<long>(data[index + 3]) << 32) |
                  (static_cast<long>(data[index + 4]) << 24) | (static_cast<long>(data[index + 5]) << 16) |
                  (static_cast<long>(data[index + 6]) << 8) | static_cast<long>(data[index + 7]);
    index += 8;

    return result;
  }

  int readVarInt() { return VarInt::readVarInt(data, index); }
  std::string readString() { return String::readString(data, index); }

  void end() {
    if (data.size() != index) {
      throw std::runtime_error("Packet length is more than expected");
    }
  }
};
