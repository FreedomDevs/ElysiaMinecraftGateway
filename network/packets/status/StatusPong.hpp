#pragma once

#include "../../BasePacket.hpp"
#include "../../PacketWriter.hpp"

class StatusPong : public BasePacket {
public:
  static PacketWriter encode(long payload) {
    PacketWriter writer;
    writer.writeLong(payload);
    writer.setPacketId(1);
    return writer;
  };
};
