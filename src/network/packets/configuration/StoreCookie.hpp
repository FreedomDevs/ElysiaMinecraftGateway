#pragma once

#include "../../BasePacket.hpp"
#include "../../PacketWriter.hpp"

class StoreCookie : public BasePacket {
public:
  static void encode_separated(const std::string_view identificator, const std::string_view data) {
    int packet_size = VarInt::varint_size(10) + VarInt::varint_size(identificator.size()) + identificator.size() +
                      VarInt::varint_size(data.size()) + data.size();
    PacketWriter::reserve_size_in_data(PacketSize::VarInt + packet_size);

    size_t pos = packet_data.size();
    VarInt::writeVarInt(packet_size, packet_data);
    VarInt::writeVarInt(10, packet_data);
    String::writeString(identificator, packet_data);

    iovec io;
    io.iov_base = PacketWriter::build_tagged_int(pos);
    io.iov_len = packet_data.size() - pos;

    packet_iovec.push_back(io);
    packet_iovec.push_back(iovec{(void *)data.data(), data.size()});
  };
};
