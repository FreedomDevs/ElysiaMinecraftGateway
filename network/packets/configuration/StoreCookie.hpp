#pragma once

#include "../../BasePacket.hpp"
#include "../../PacketWriter.hpp"

class StoreCookie : public BasePacket {
private:
  PacketWriter writer;

public:
  void encode(std::string identificator, std::string data) {
    writer.writeString(identificator);
    writer.writeString(data);
    writer.setPacketId(10);
  };

  PacketWriter getPacketWriter() { return writer; }
};
