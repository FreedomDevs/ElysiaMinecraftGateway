#pragma once

#include "../../BasePacket.hpp"
#include "../../PacketReader.hpp"
#include "../../PacketWriter.hpp"

class StatusResponse : public BasePacket {
  std::string_view data;

public:
  static PacketWriter encode(std::string statusdata) {
    PacketWriter writer;
    writer.writePacketId(0);
    writer.writeString(statusdata);
    return writer;
  };

  void decode(PacketReader reader) {
    this->data = reader.readString();
    reader.end();
  };
};
