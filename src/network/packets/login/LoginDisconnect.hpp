#pragma once

#include "../../BasePacket.hpp"
#include "../../PacketWriter.hpp"

class LoginDisconnect : public BasePacket {
public:
  static PacketWriter encode(std::string data) {
    PacketWriter writer;
    writer.writePacketId(2);
    writer.writeArray(std::span<unsigned char>((unsigned char *)data.data(), data.size()));

    return writer;
  };
};
