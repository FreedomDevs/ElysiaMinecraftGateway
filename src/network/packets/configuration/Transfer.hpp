#pragma once

#include "../../BasePacket.hpp"
#include "../../PacketWriter.hpp"
#include <cstdint>

class Transfer : public BasePacket {
public:
  static void encode(std::string domain, uint16_t port) {
    SPDLOG_DEBUG("2");
    int packet_size = VarInt::varint_size(11) + VarInt::varint_size(domain.size()) + domain.size() + VarInt::varint_size(port);
    SPDLOG_DEBUG("3");
    PacketWriter::reserve_size_in_data(PacketSize::VarInt + packet_size);

    SPDLOG_DEBUG("4");
    size_t pos = packet_data.size();
    VarInt::writeVarInt(packet_size, packet_data);
    VarInt::writeVarInt(11, packet_data);
    String::writeString(domain, packet_data);
    VarInt::writeVarInt(port, packet_data);

    SPDLOG_DEBUG("5");
    iovec io;
    io.iov_base = PacketWriter::build_tagged_int(pos);
    io.iov_len = packet_data.size() - pos;

    packet_iovec.push_back(io);
  };
};
