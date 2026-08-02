#pragma once

#include "../../BasePacket.hpp"
#include "../../PacketWriter.hpp"
#include <cstdint>

class Transfer : public BasePacket {
private:
  PacketWriter writer;

public:
  void encode(std::string domain, uint16_t port) {
    writer.writeString(domain);
    writer.writeVarInt(port);
    writer.setPacketId(11);
  };

  PacketWriter getPacketWriter() { return writer; }
};
