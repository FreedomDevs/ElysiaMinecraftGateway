#pragma once
#include "../BasePacket.hpp"
#include "../PacketReader.hpp"
#include "../PacketWriter.hpp"
#include "../types/Primitives.hpp"
#include <stdexcept>
#include <string>
#include <sys/uio.h>

enum class ConnectionReason : int { Status = 1, Connnect = 2, Transfer = 3 };

class HandShake : public BasePacket {
private:
  int protocol_version;
  std::string server_address;
  unsigned short server_port;
  ConnectionReason connection_reason;

public:
  void decode(PacketReader &reader) {
    protocol_version = reader.readVarInt();
    server_address = reader.readString();
    server_port = reader.readUnsignedShort();
    connection_reason = ConnectionReason(reader.readVarInt());
    if ((int)connection_reason < 1 or (int) connection_reason > 3) {
      throw std::runtime_error("Incorrect connection_reason");
    }

    reader.end();
  };

  static void encode(int protocol_version, const std::string &server_address, unsigned short server_port,
                     ConnectionReason connection_reason) {
    int sizeofdata =                                 //
        VarInt::varint_size(0) +                     // Id
        VarInt::varint_size(protocol_version) +      // Protocol Version
        VarInt::varint_size(server_address.size()) + // Strlen
        server_address.size() +                      // Str
        PacketSize::Short + 1;                       // Port + ConnectionReason

    PacketWriter::reserve_size_in_data(PacketSize::VarInt + sizeofdata); // Size + data

    size_t pos = packet_data.size();
    VarInt::writeVarInt(sizeofdata, packet_data);
    VarInt::writeVarInt(0, packet_data);
    VarInt::writeVarInt(protocol_version, packet_data);
    String::writeString(server_address, packet_data);
    Primitives::writeUnsignedShort(server_port, packet_data);
    VarInt::writeVarInt((int)connection_reason, packet_data);

    iovec io;
    io.iov_base = PacketWriter::build_tagged_int(pos);
    io.iov_len = packet_data.size() - pos;

    packet_iovec.push_back(io);
  }

  int getProtocolVersion() { return protocol_version; }
  std::string getServerAddress() { return server_address; }
  unsigned short getServerPort() { return server_port; }
  ConnectionReason getConnectionReason() { return connection_reason; }
  bool isSnapshot() { return (protocol_version & 0x40000000) != 0; }
};
