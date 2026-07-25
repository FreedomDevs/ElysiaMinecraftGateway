#pragma once
#include "../BasePacket.hpp"
#include "../PacketReader.hpp"
#include <stdexcept>
#include <string>

enum class ConnectionReason : int { Status = 1, Connnect = 2, Transfer = 3 };

class HandShake : public BasePacket {
private:
  int protocol_version;
  std::string server_address;
  unsigned short server_port;
  ConnectionReason connection_reason;

public:
  void decode(PacketReader reader) {
    protocol_version = reader.readVarInt();
    server_address = reader.readString();
    server_port = reader.readUnsignedShort();
    connection_reason = ConnectionReason(reader.readVarInt());
    if ((int)connection_reason < 1 or (int) connection_reason > 3) {
      throw std::runtime_error("Incorrect connection_reason");
    }

    reader.end();
  };

  int getProtocolVersion() { return protocol_version; }
  std::string getServerAddress() { return server_address; }
  unsigned short getServerPort() { return server_port; }
  ConnectionReason getConnectionReason() { return connection_reason; }
};
