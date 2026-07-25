#ifndef NETWORK_PACKETS_STATUS_STATUSRESPONSE_HPP
#define NETWORK_PACKETS_STATUS_STATUSRESPONSE_HPP

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

#endif // NETWORK_PACKETS_STATUS_STATUSRESPONSE_HPP
