#pragma once

#include "../../BasePacket.hpp"
#include "../../PacketWriter.hpp"

class StatusPong : public BasePacket {
public:
  static PacketWriter encode(long payload) {
    PacketWriter writer;
    writer.writePacketId(1);
    writer.writeLong(payload);
    return writer;
  };
};
