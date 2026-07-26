#pragma once

#include "../../BasePacket.hpp"
#include "../../PacketReader.hpp"

class LoginAcknowledged : public BasePacket {
private:
public:
  void decode(PacketReader reader) { reader.end(); };
};
