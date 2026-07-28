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
#include <ctime>
#include <curl/multi.h>
#include <fcntl.h>
#include <iostream>
#include <netinet/in.h>
#include <optional>
#include <span>
#include <stdexcept>
#include <sys/epoll.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/timerfd.h>
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

int epoll_fd;
int timer_fd;
CURLM *multi_handle;

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

static inline void onPacket(MinecraftClient *client, PacketReader &packet, size_t client_index) {
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
      throw std::runtime_error("Incorrect packet id " + std::to_string(packet.getPacketId()));
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
    } else {
      throw std::runtime_error("Incorrect packet id " + std::to_string(packet.getPacketId()));
    }
  } else if (client->state == ConnectionState::Configuration) {
    if (packet.getPacketId() == 0 || packet.getPacketId() == 2) {
      return;
    } else if (packet.getPacketId() == 8) {
      std::cout << "Client closed connection";
      clients.erase(clients.begin() + client_index);
      close(client->fd);
    } else {
      throw std::runtime_error("Incorrect packet id " + std::to_string(packet.getPacketId()));
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

      onPacket(client, reader, client_index);

      memmove(client->buf.data(), client->buf.data() + *ret, client->buf.size() - *ret);
      client->buf.resize(client->buf.size() - *ret);
    }
  } catch (const std::exception &e) {
    std::cerr << "An exception was occured while pasing client data: " << e.what() << std::endl;
    clients.erase(clients.begin() + client_index);
    close(client->fd);
  }
}

static int cb_socket_action(CURL *easy, curl_socket_t s, int action, void *userp, void *socketp) {
  epoll_event ev{};
  ev.data.fd = s;

  if (action == CURL_POLL_IN) {
    ev.events = EPOLLIN;
  } else if (action == CURL_POLL_OUT) {
    ev.events = EPOLLOUT;
  } else if (action == CURL_POLL_INOUT) {
    ev.events = EPOLLIN | EPOLLOUT;
  } else if (action == CURL_POLL_REMOVE) {
    // Удаляем сокет из epoll
    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, s, nullptr);
    return 0;
  }

  // Если сокет уже регистрировался ранее (socketp != nullptr), делаем MOD, иначе ADD
  int op = (socketp != nullptr) ? EPOLL_CTL_MOD : EPOLL_CTL_ADD;
  epoll_ctl(epoll_fd, op, s, &ev);

  // Сохраняем маркер, что сокет зарегистрирован
  curl_multi_assign(multi_handle, s, (void *)1);

  return 0;
}

static int cb_timer_action(CURLM *multi, long timeout_ms, void *userp) {
  // struct itimerspec задает, через сколько сработает timerfd
  struct itimerspec its{};

  if (timeout_ms < 0) {
    // timeout_ms < 0 означает, что curl просит отменить текущий таймер
    // Передаем нулевой its — это выключает timerfd
    timerfd_settime(timer_fd, 0, &its, nullptr);

  } else {
    // Если curl просит 0 мс, взводим на 1 нс (минимально возможное время),
    // чтобы epoll_wait сработал сразу на следующей итерации
    if (timeout_ms == 0) {
      its.it_value.tv_nsec = 1;
    } else {
      // Переводим миллисекунды в секунды и наносекунды
      its.it_value.tv_sec = timeout_ms / 1000;
      its.it_value.tv_nsec = (timeout_ms % 1000) * 1000000;
    }

    // Заряжаем timerfd!
    timerfd_settime(timer_fd, 0, &its, nullptr);
  }

  return 0;
}

int main() {
  readAndWriteDataPack();
  int tcpfd = initTcp(PORT);
  std::cout << "Запущен сокет на порту " << PORT << std::endl;

  epoll_fd = epoll_create1(O_CLOEXEC);
  timer_fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);

  struct epoll_event ev;
  ev.events = EPOLLIN;
  ev.data.fd = tcpfd;
  epoll_ctl(epoll_fd, EPOLL_CTL_ADD, tcpfd, &ev);

  multi_handle = curl_multi_init();
  curl_multi_setopt(multi_handle, CURLMOPT_SOCKETDATA, nullptr);
  curl_multi_setopt(multi_handle, CURLMOPT_SOCKETFUNCTION, cb_socket_action);
  curl_multi_setopt(multi_handle, CURLMOPT_TIMERDATA, nullptr);
  curl_multi_setopt(multi_handle, CURLMOPT_TIMERFUNCTION, cb_timer_action);

  struct epoll_event events[32];
  while (1) {
    int count = epoll_wait(epoll_fd, events, 32, -1);

    for (int i = 0; i < count; i++) {
      int fd = events[i].data.fd;
      uint32_t event = events[i].events;

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
        epoll_ctl(epoll_fd, EPOLL_CTL_ADD, clientfd, &ev);
      } else if (fd == timer_fd) {
        // 1. Обязательно "прочитываем" timerfd, чтобы сбросить его готовность в epoll
        uint64_t expirations;
        read(timer_fd, &expirations, sizeof(expirations));

        // 2. Сообщаем curl, что время вышло!
        int running_handles = 0;
        curl_multi_socket_action(multi_handle, CURL_SOCKET_TIMEOUT, 0, &running_handles);

        // 3. Проверяем, не завершился ли какой-то запрос
        // check_completed_requests();
      } else {
        for (size_t i = 0; i < clients.size(); i++) {
          struct MinecraftClient *client = &clients[i];
          if (fd == client->fd) {
            onClientUpdate(client, i);
          }

          break;
        }

        int action_mask = 0;
        if (event & EPOLLIN) {
          action_mask |= CURL_CSELECT_IN; // Есть данные для чтения
        }
        if (event & EPOLLOUT) {
          action_mask |= CURL_CSELECT_OUT; // Сокет готов к записи
        }
        if (event & (EPOLLERR | EPOLLHUP)) {
          action_mask |= CURL_CSELECT_ERR; // Ошибка или разрыв соединения
        }

        // 2. Уведомляем curl, что на сокете fd произошло событие action_mask
        int running_handles = 0;
        curl_multi_socket_action(multi_handle, fd, action_mask, &running_handles);

        // 3. Проверяем, не завершился ли запрос в результате этого I/O
        // check_completed_requests();
      }
    }
  }

  return 0;
}
