#pragma once

#include "../../BasePacket.hpp"
#include "../../PacketWriter.hpp"

class StatusResponse : public BasePacket {
public:
  static PacketWriter encode(std::string statusdata) {
    PacketWriter writer;
    writer.writeString(statusdata);
    writer.setPacketId(0);
    return writer;
  };
};
