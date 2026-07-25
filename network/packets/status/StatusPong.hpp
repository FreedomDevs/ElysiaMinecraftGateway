#pragma once

#include "../../BasePacket.hpp"
#include "../../PacketWriter.hpp"

class StatusPong : public BasePacket {
private:
  PacketWriter writer;

public:
  void encode(long payload) {
    writer.writeLong(payload);
    writer.setPacketId(1);
  };

  PacketWriter getPacketWriter() { return writer; }
};
