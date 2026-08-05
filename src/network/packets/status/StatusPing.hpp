#pragma once

#include "../../BasePacket.hpp"
#include "../../PacketReader.hpp"

class StatusPing : public BasePacket {
private:
  long payload;

public:
  void decode(PacketReader reader) {
    payload = reader.readLong();
    reader.end();
  };

  long getPayload() { return payload; }
};
