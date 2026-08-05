#pragma once

#include "../../BasePacket.hpp"
#include "../../PacketWriter.hpp"

class StoreCookie : public BasePacket {
public:
  static PacketWriter encode(std::string identificator, std::string data) {
    PacketWriter writer;
    writer.writePacketId(10);
    writer.writeString(identificator);
    writer.writeString(data);
    return writer;
  };
};
