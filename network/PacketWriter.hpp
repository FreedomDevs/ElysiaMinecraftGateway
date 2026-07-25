#pragma once

#include "types/String.hpp"
#include "types/VarInt.hpp"
#include <string>
#include <vector>

class PacketWriter {
private:
  std::vector<unsigned char> data;
  int packet_id;

public:
  PacketWriter() {}
  void writeUncompressed() {
    std::vector<unsigned char> old_data = data;
    data = std::vector<unsigned char>();
    std::vector<unsigned char> test_buffer;
    VarInt::writeVarInt(packet_id, test_buffer);
    writeVarInt(old_data.size() + test_buffer.size());
    append_vector(test_buffer);

    append_vector(old_data);
  }
  void writeCompressed() {}

  const std::vector<unsigned char> &getData() { return data; }
  void setPacketId(int packet_id) { this->packet_id = packet_id; }

  void writeUnsignedShort(unsigned short data) {
    this->data.push_back(data);
    this->data.push_back(data >> 8);
  }
  void writeLong(long data) {
    this->data.push_back(data >> 56);
    this->data.push_back(data >> 48);
    this->data.push_back(data >> 40);
    this->data.push_back(data >> 32);
    this->data.push_back(data >> 24);
    this->data.push_back(data >> 16);
    this->data.push_back(data >> 8);
    this->data.push_back(data);
  }
  void writeVarInt(int data) { VarInt::writeVarInt(data, this->data); }
  void writeString(std::string data) { String::writeString(data, this->data); }

  void append_vector(std::vector<unsigned char> vector) {
    data.insert(data.end(), vector.begin(), vector.end());
  }
};
