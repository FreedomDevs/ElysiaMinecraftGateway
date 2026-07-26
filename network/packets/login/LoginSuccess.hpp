#pragma once

#include "../../BasePacket.hpp"
#include "../../PacketWriter.hpp"

class LoginSuccess : public BasePacket {
private:
  PacketWriter writer;

public:
  void encode(long UUID_part1, long UUID_part2, std::string username) {
    // GameProfile
    writer.writeLong(UUID_part1);
    writer.writeLong(UUID_part2);
    writer.writeString(username);
    writer.writeVarInt(0);

    writer.writeLong(UUID_part1);
    writer.writeLong(UUID_part2);

    writer.setPacketId(2);
  };

  PacketWriter getPacketWriter() { return writer; }
};
