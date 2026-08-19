#pragma once

#include "types/String.hpp"
#include "types/VarInt.hpp"
#include <spdlog/spdlog.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <unistd.h>
#include <vector>

static inline std::vector<iovec> packet_iovec(4);
static inline std::vector<unsigned char> packet_data(4096);

static inline msghdr msgtosend;
inline msghdr *gen_msg_by_iovec() {
  msgtosend.msg_iov = packet_iovec.data();
  msgtosend.msg_iovlen = packet_iovec.size();
  return &msgtosend;
}

static inline std::vector<unsigned char> minecraft_header(5);

namespace PacketSize {
constexpr size_t VarInt = 5;
constexpr size_t VarLong = 10;
constexpr size_t Short = 2;
constexpr size_t Long = 8;
constexpr size_t String(const std::string_view str) { return VarInt + str.size(); }
} // namespace PacketSize

class PacketWriter {
private:
  std::vector<unsigned char> data;

  static constexpr uintptr_t TAG_MASK = 0x1;

public:
  PacketWriter() {}

  size_t size() noexcept { return data.size(); }

  void generate_iovec(iovec *__restrict io) { // Должен быть доступен io и io+1
    minecraft_header.clear();
    VarInt::writeVarInt(data.size(), minecraft_header);
    io->iov_base = minecraft_header.data();
    io->iov_len = minecraft_header.size();

    (io + 1)->iov_base = data.data();
    (io + 1)->iov_len = data.size();
  }

  static void *build_tagged_int(int num) { return (void *)(((size_t)num << 32) | TAG_MASK); }
  static void reserve_size_in_data(size_t size) {
    if (packet_data.size() + size <= packet_data.capacity())
      return;

    packet_data.reserve(packet_data.size() + size);
  }
  static void build_iovec() {
    for (auto &i : packet_iovec) {
      if (((size_t)i.iov_base & TAG_MASK) != 0) {
        i.iov_base = packet_data.data() + ((size_t)i.iov_base >> 32);
      }
    }
  }
  static void clean_packets() {
    packet_data.clear();
    packet_iovec.clear();
  }

  static void send_to_fd(const int fd) {
    build_iovec();
    if (packet_iovec.size() == 1)
      write(fd, packet_iovec[0].iov_base, packet_iovec[0].iov_len);
    else
      writev(fd, packet_iovec.data(), packet_iovec.size());

    clean_packets();
  }

  static void send_with_more(const int fd) {
    build_iovec();
    if (packet_iovec.size() == 1)
      send(fd, packet_iovec[0].iov_base, packet_iovec[0].iov_len, MSG_MORE);
    else
      sendmsg(fd, gen_msg_by_iovec(), MSG_MORE);

    clean_packets();
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
  void writeString(const std::string_view data) { String::writeString(data, this->data); }

  void writeArray(std::span<const unsigned char> vector) { data.insert(data.end(), vector.begin(), vector.end()); }
};
