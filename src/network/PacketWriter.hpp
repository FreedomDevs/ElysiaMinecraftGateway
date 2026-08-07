#pragma once

#include "types/String.hpp"
#include "types/VarInt.hpp"
#include <spdlog/spdlog.h>
#include <stdexcept>
#include <string>
#include <sys/uio.h>
#include <vector>

static inline std::vector<unsigned char> minecraft_header(5);

class PacketWriter {
private:
  std::vector<unsigned char> data;
  bool has_length = false;

public:
  PacketWriter() {}
  void insert_length_in_start() {
    std::vector<unsigned char> old_data = data;
    data = std::vector<unsigned char>();
    writeVarInt(old_data.size());
    writeArray(old_data);
    has_length = true;
  }

  unsigned char *get_data() {
    if (!has_length) [[unlikely]]
      throw std::runtime_error("Вызван get_data, без объявленного size");

    return data.data();
  }
  size_t size() noexcept { return data.size(); }

  void generate_iovec(iovec *__restrict io) { // Должен быть доступен io и io+1
    minecraft_header.clear();
    VarInt::writeVarInt(data.size(), minecraft_header);
    io->iov_base = minecraft_header.data();
    io->iov_len = minecraft_header.size();

    (io + 1)->iov_base = data.data();
    (io + 1)->iov_len = data.size();
  }

  std::vector<unsigned char> &getData() { return data; }

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
  void writePacketId(int packet_id) { return writeVarInt(packet_id); }
  void writeVarInt(int data) { VarInt::writeVarInt(data, this->data); }
  void writeString(std::string data) { String::writeString(data, this->data); }

  void writeArray(std::span<const unsigned char> vector) { data.insert(data.end(), vector.begin(), vector.end()); }
};
