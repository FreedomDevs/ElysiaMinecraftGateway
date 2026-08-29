#pragma once

#include "../../BasePacket.hpp"
#include "../../PacketReader.hpp"
#include "../../PacketWriter.hpp"
#include <sys/uio.h>

class StatusResponse : public BasePacket {
  std::string_view data;

public:
  // Записывает тупо указатель на statusdata, важно что statusdata не должен случайно удалится в какой-то момент
  static void encode_separeted(const std::string_view statusdata) {
    int packet_size = VarInt::varint_size(0) + VarInt::varint_size(statusdata.size()) + statusdata.size();
    PacketWriter::reserve_size_in_data(PacketSize::VarInt + packet_size);

    size_t pos = packet_data.size();
    VarInt::writeVarInt(packet_size, packet_data);
    VarInt::writeVarInt(0, packet_data);
    VarInt::writeVarInt(statusdata.size(), packet_data);

    iovec io;
    io.iov_base = PacketWriter::build_tagged_int(pos);
    io.iov_len = packet_data.size() - pos;

    packet_iovec.push_back(io);
    packet_iovec.push_back(iovec{(void *)statusdata.data(), statusdata.size()});
  };

  void decode(PacketReader reader) {
    this->data = reader.readString();
    reader.end();
  };
};
