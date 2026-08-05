#pragma once

#include "../../BasePacket.hpp"
#include "../../PacketWriter.hpp"
#include <cstdint>

class Transfer : public BasePacket {
public:
  static PacketWriter encode(std::string domain, uint16_t port) {
    PacketWriter writer;
    writer.writePacketId(11);
    writer.writeString(domain);
    writer.writeVarInt(port);
    return writer;
  };
};
