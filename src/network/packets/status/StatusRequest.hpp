#pragma once

#include "../../BasePacket.hpp"
#include "../../PacketReader.hpp"
#include "../../PacketWriter.hpp"

class StatusRequest : public BasePacket {
private:
public:
  void decode(PacketReader reader) { reader.end(); };
};
