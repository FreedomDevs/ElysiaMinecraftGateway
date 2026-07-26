#pragma once

#include "../../BasePacket.hpp"
#include "../../PacketWriter.hpp"

class StatusResponse : public BasePacket {
private:
  PacketWriter writer;

public:
  void encode(std::string statusdata) {
    writer.writeString(statusdata);
    writer.setPacketId(0);
  };

  PacketWriter getPacketWriter() { return writer; }
};
