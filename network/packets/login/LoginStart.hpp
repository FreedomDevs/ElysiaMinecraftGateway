#pragma once
#include "../../BasePacket.hpp"
#include "../../PacketReader.hpp"
#include <string>

enum class ConnectionReason : int { Status = 1, Connnect = 2, Transfer = 3 };

class LoginStart : public BasePacket {
private:
  std::string username;
  long UUID_part1;
  long UUID_part2;

public:
  void decode(PacketReader reader) {
    username = reader.readString();
    UUID_part1 = reader.readLong();
    UUID_part2 = reader.readLong();

    reader.end();
  };

  std::string getUsername() { return username; }
  long getUUID1() { return UUID_part1; }
  long getUUID2() { return UUID_part2; }
};
