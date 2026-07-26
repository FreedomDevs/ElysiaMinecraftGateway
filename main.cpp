#include "network/initSocket.h"
#include "network/packets/Handshake.hpp"
#include "network/packets/login/LoginAcknowledged.hpp"
#include "network/packets/login/LoginStart.hpp"
#include "network/packets/login/LoginSuccess.hpp"
#include "network/packets/status/StatusPing.hpp"
#include "network/packets/status/StatusPong.hpp"
#include "network/packets/status/StatusRequest.hpp"
#include "network/packets/status/StatusResponse.hpp"
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <netinet/in.h>
#include <optional>
#include <span>
#include <stdexcept>
#include <sys/epoll.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

constexpr unsigned short PORT = 25565;

enum class ConnectionState { HandShake, Status, Login, Configuration, Play };

struct MinecraftClient {
  int fd;
  ConnectionState state;
  std::string domain;
  std::vector<unsigned char> buf;
  bool isLoginSuccess;
};

std::vector<MinecraftClient> clients;

static std::span<char> dialog;

static inline void readAndWriteDataPack() {
  int filefd = open("dialog", O_RDONLY);
  if (filefd == -1) {
    perror("Не удалось открывать файл");
    _exit(1);
  }

  struct stat sb;
  if (fstat(filefd, &sb) == -1) {
    std::perror("fstat error");
    close(filefd);
    _exit(1);
  }

  char *addr = static_cast<char *>(mmap(nullptr, sb.st_size, PROT_READ, MAP_PRIVATE, filefd, 0));
  if (addr == MAP_FAILED) {
    std::perror("mmap error");
    close(filefd);
    _exit(1);
  }

  close(filefd);

  dialog = std::span<char>(addr, sb.st_size);
}

static inline void onPacket(MinecraftClient *client, PacketReader &packet) {
  if (client->state == ConnectionState::HandShake) {
    if (packet.getPacketId() != 0)
      throw std::runtime_error("Incorrect packet id");

    HandShake handshake;
    handshake.decode(packet);

    if (handshake.getServerPort() != PORT) {
      throw std::runtime_error("Incorrect connection port: " + std::to_string(handshake.getServerPort()));
    }

    client->domain = handshake.getServerAddress();
    std::cout << "Получен Handshake, адрес: " << handshake.getServerAddress() << ", версия протокола: " << handshake.getProtocolVersion()
              << ", причина подключения: " << (int)handshake.getConnectionReason() << std::endl;

    if (handshake.getConnectionReason() == ConnectionReason::Status)
      client->state = ConnectionState::Status;
    else
      client->state = ConnectionState::Login;
  } else if (client->state == ConnectionState::Status) {
    if (packet.getPacketId() == 0) {
      StatusRequest req;
      req.decode(packet);

      std::cout << "Получен Status" << std::endl;

      StatusResponse resp;
      resp.encode(R"(
                  {
    "version": {
        "name": "1.21.11",
        "protocol": 774
    },
    "players": {
        "max": 20,
        "online": 1,
        "sample": [
            {
                "name": "mikinol",
                "id": "4566e69f-c907-48ee-8d71-d7ba5aa00d20"
            }
        ]
    },
    "description": {
        "text": "Hello, world!"
    },
    "enforcesSecureChat": false
}
                  )");

      PacketWriter writer = resp.getPacketWriter();
      writer.writeUncompressed();

      write(client->fd, writer.getData().data(), writer.getData().size());
    } else if (packet.getPacketId() == 1) {
      StatusPing req;
      req.decode(packet);

      StatusPong resp;
      resp.encode(req.getPayload());

      PacketWriter writer = resp.getPacketWriter();
      writer.writeUncompressed();

      write(client->fd, writer.getData().data(), writer.getData().size());
    } else {
      throw std::runtime_error("Incorrect packet id");
    }
  } else if (client->state == ConnectionState::Login) {
    if (packet.getPacketId() == 0) {
      LoginStart req;
      req.decode(packet);

      LoginSuccess resp;
      resp.encode(req.getUUID1(), req.getUUID2(), req.getUsername());

      PacketWriter writer = resp.getPacketWriter();
      writer.writeUncompressed();
      write(client->fd, writer.getData().data(), writer.getData().size());
      client->isLoginSuccess = true;

    } else if (packet.getPacketId() == 3) {
      LoginAcknowledged res;
      res.decode(packet);

      if (!client->isLoginSuccess) {
        throw std::runtime_error("Client idet v pered paravoza");
      }

      client->state = ConnectionState::Configuration;

      PacketWriter writer;

      writer.writeArray(std::span<unsigned char>{reinterpret_cast<unsigned char *>(dialog.data()), dialog.size()});
      writer.setPacketId(18);
      writer.writeUncompressed();
      write(client->fd, writer.getData().data(), writer.getData().size());
    }

  } else if (client->state == ConnectionState::Configuration) {
    if (packet.getPacketId() == 0) {
      return;
    }
  } else {
    throw std::runtime_error("Incorrect state");
  }
}

static inline void onClientUpdate(MinecraftClient *client, size_t client_index) {

  long ret;
  char temp_buf[4096];
  while ((ret = read(client->fd, temp_buf, sizeof(temp_buf))) > 0) {
    client->buf.insert(client->buf.end(), temp_buf, temp_buf + ret);
  };

  if (ret < 0 and errno != EAGAIN) {
    perror("Client error(connection closed): ");
    clients.erase(clients.begin() + client_index);
    close(client->fd);
    return;
  }

  if (ret == 0) {
    std::cout << "Client closed connection" << std::endl;
    clients.erase(clients.begin() + client_index);
    close(client->fd);
    return;
  }

  try {
    while (true) {
      std::span<unsigned char> view(client->buf.data(), client->buf.size());
      PacketReader reader(view);

      // std::cout << to_hex(client->buf.data(), client->buf.size()) << std::endl;

      std::optional<size_t> ret = reader.readUncompressed();
      if (!ret.has_value())
        break;

      onPacket(client, reader);

      memmove(client->buf.data(), client->buf.data() + *ret, client->buf.size() - *ret);
      client->buf.resize(client->buf.size() - *ret);
    }
  } catch (const std::exception &e) {
    std::cerr << "An exception was occured while pasing client data: " << e.what() << std::endl;
    clients.erase(clients.begin() + client_index);
    close(client->fd);
  }
}

int main() {
  readAndWriteDataPack();
  int tcpfd = initTcp(PORT);
  std::cout << "Запущен сокет на порту " << PORT << std::endl;

  int epfd = epoll_create(32);

  struct epoll_event ev;
  ev.events = EPOLLIN;
  ev.data.fd = tcpfd;
  epoll_ctl(epfd, EPOLL_CTL_ADD, tcpfd, &ev);

  struct epoll_event events[32];
  while (1) {
    int count = epoll_wait(epfd, events, 32, -1);

    for (int i = 0; i < count; i++) {
      int fd = events[i].data.fd;

      if (fd == tcpfd) {
        int clientfd = accept4(fd, NULL, NULL, SOCK_NONBLOCK | SOCK_CLOEXEC);
        if (clientfd < 0) {
          perror("client error");
          continue;
        }

        struct MinecraftClient client;
        client.fd = clientfd;
        client.state = ConnectionState::HandShake;
        printf("client %d connected\n", clientfd);

        clients.push_back(client);

        ev.events = EPOLLIN;
        ev.data.fd = clientfd;
        epoll_ctl(epfd, EPOLL_CTL_ADD, clientfd, &ev);
      } else {
        for (size_t i = 0; i < clients.size(); i++) {
          struct MinecraftClient *client = &clients[i];
          if (fd == client->fd) {
            onClientUpdate(client, i);
          }
        }
      }
    }
  }

  return 0;
}
