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
#include <nlohmann/json.hpp>
#include <optional>
#include <span>
#include <spanstream>
#include <sstream>
#include <stdexcept>
#include <sys/epoll.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/timerfd.h>
#include <unistd.h>
#include <vector>

constexpr unsigned short PORT = 25565;
constexpr unsigned short WEB_PORT = 8090;

enum class ConnectionState { HandShake, Status, Login, Configuration, Play };

struct MinecraftClient {
  int fd;
  ConnectionState state;
  std::string domain;
  std::vector<unsigned char> buf;
  bool isLoginSuccess;
};

struct WebClient {
  int fd;
};

struct RequestContext {
  std::string token;
  std::string response_body;
  bool is_check_refresh;
  int res_fd;
  struct curl_slist *headers = nullptr;

  ~RequestContext() {
    if (headers) {
      curl_slist_free_all(headers);
    }
  }
};

std::vector<MinecraftClient> clients;
std::vector<WebClient> web_clients;

static std::span<unsigned char> dialog;

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

  dialog = std::span<unsigned char>((unsigned char *)addr, sb.st_size);
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
      writer.writeArray(dialog);
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
    close(client->fd);
    clients.erase(clients.begin() + client_index);
    return;
  }

  if (ret == 0) {
    std::cout << "Client closed connection" << std::endl;
    close(client->fd);
    clients.erase(clients.begin() + client_index);
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
    close(client->fd);
    clients.erase(clients.begin() + client_index);
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

static inline void check_completed_requests() {
  CURLMsg *msg = nullptr;
  int msgs_left = 0;

  // Вычитываем все завершившиеся запросы из очереди cURL
  while ((msg = curl_multi_info_read(multi_handle, &msgs_left))) {
    if (msg->msg == CURLMSG_DONE) {
      CURL *easy_handle = msg->easy_handle;
      CURLcode result = msg->data.result;

      // 1. Достаем наш контекст с токеном обратно!
      RequestContext *ctx = nullptr;
      curl_easy_getinfo(easy_handle, CURLINFO_PRIVATE, &ctx);

      if (result == CURLE_OK) {
        long response_code = 0;
        curl_easy_getinfo(easy_handle, CURLINFO_RESPONSE_CODE, &response_code);

        std::cout << "Запрос завершен! HTTP status: " << response_code << "\n";
        std::cout << "Токен: " << ctx->token << "\n";
        std::cout << "Is refresh: " << ctx->is_check_refresh << "\n";
        std::cout << "fd: " << ctx->res_fd << "\n";
        std::cout << "Ответ от сервера: " << ctx->response_body << "\n";

        if (ctx->is_check_refresh) {
          const char response[] = "HTTP/1.1 200 OK\r\n"
                                  "Content-Type: text/html; charset=utf-8\r\n"
                                  "Connection: close\r\n"
                                  "\r\n"
                                  "<!DOCTYPE html>"
                                  "<html>"
                                  "<head>"
                                  "<meta charset=\"utf-8\">"
                                  "<title>Elysia</title>"
                                  "</head>"
                                  "<body style=\"font-family:sans-serif;text-align:center;margin-top:100px;\">"
                                  "<h1>✅ Авторизация успешно завершена</h1>"
                                  "<p>Теперь вернитесь в Minecraft.</p>"
                                  "<p>Вход будет продолжен автоматически.</p>"
                                  "</body>"
                                  "</html>";

          sendto(ctx->res_fd, response, sizeof(response) - 1, MSG_NOSIGNAL | MSG_MORE, 0, 0);
          close(ctx->res_fd);
          for (auto i = web_clients.begin(); i < web_clients.end(); i++) {
            if (i->fd == ctx->res_fd) {
              web_clients.erase(i);
              break;
            }
          }
        }
      } else {
        std::cerr << "Ошибка cURL: " << curl_easy_strerror(result) << "\n";
      }

      // 2. ОБЯЗАТЕЛЬНО: удаляем handle из multi и чистим память
      curl_multi_remove_handle(multi_handle, easy_handle);
      curl_easy_cleanup(easy_handle);
      delete ctx; // Удаляем созданный контекст
    }
  }
}

size_t write_callback(char *ptr, size_t size, size_t nmemb, void *userdata) {
  size_t total_size = size * nmemb;
  auto *ctx = static_cast<RequestContext *>(userdata);

  // Дописываем пришедшие байты в наш response_body
  ctx->response_body.append(ptr, total_size);

  return total_size;
}

static inline void onWebClientUpdate(WebClient *client, size_t client_index) {
  std::string token;

  long ret;
  char temp_buf[4096];
  int pos = 0;
  while ((ret = read(client->fd, temp_buf + pos, sizeof(temp_buf) - pos)) > 0) {
    pos += ret;
  };

  if (ret < 0 and errno != EAGAIN) {
    perror("Client error(connection closed): ");
    close(client->fd);
    web_clients.erase(web_clients.begin() + client_index);
    return;
  }

  if (ret == 0) {
    std::cout << "Client closed connection" << std::endl;
    close(client->fd);
    web_clients.erase(web_clients.begin() + client_index);
    return;
  }

  try {
    std::string_view request(temp_buf, temp_buf + pos);

    ssize_t end = request.find("\r\n");
    std::string_view firstLine = request.substr(0, end);
    ssize_t q = firstLine.find('?');
    if (q == (long)std::string::npos) {
      return;
    }

    size_t http = firstLine.find(" HTTP/", q);

    std::string_view query = firstLine.substr(q + 1, http - q - 1);

    std::vector<std::string> parts;
    std::ispanstream ss(query);
    std::string part;

    while (std::getline(ss, part, '&')) {
      parts.push_back(part);
    }

    for (size_t i = 0; i < parts.size(); i++) {
      std::stringstream sq(parts[i]);
      std::string key, value;

      while (std::getline(sq, key, '=') && std::getline(sq, value)) {
        if (key == "token") {
          token = value;
        } else {
          std::cout << "Неизвестное значение " << part[i] << std::endl;
        }
      }
    }

    if (token.empty()) {
      const char response[] = "HTTP/1.1 200 OK\r\n"
                              "Content-Type: text/html; charset=utf-8\r\n"
                              "Connection: close\r\n"
                              "\r\n"
                              "<!DOCTYPE html>"
                              "<html>"
                              "<head>"
                              "<meta charset=\"utf-8\">"
                              "<title>Elysia</title>"
                              "</head>"
                              "<body style=\"font-family:sans-serif;text-align:center;margin-top:100px;\">"
                              "<h1>Возникли проблемы с авторизацией</h1>"
                              "<p>Обратитесь в тех поддержку платформы.</p>"
                              "</body>"
                              "</html>";

      sendto(client->fd, response, sizeof(response) - 1, MSG_NOSIGNAL | MSG_MORE, 0, 0);
      close(client->fd);
      web_clients.erase(web_clients.begin() + client_index);
      return;
    }

    CURL *easy = curl_easy_init();
    if (!easy)
      return;

    // 1. Создаём контекст в куче
    auto *ctx = new RequestContext();
    ctx->token = token;
    ctx->is_check_refresh = true;
    ctx->res_fd = client->fd;

    // 2. Формируем заголовки (если нужно)
    ctx->headers = curl_slist_append(ctx->headers, "Content-Type: application/json");

    nlohmann::json body = {{"refresh_token", token}};

    // 3. Настраиваем curl easy handle
    curl_easy_setopt(easy, CURLOPT_URL, "https://fin1-services.elysiac.fun/auth/check_refresh_token");
    curl_easy_setopt(easy, CURLOPT_HTTPHEADER, ctx->headers);

    std::string payload = body.dump();
    curl_easy_setopt(easy, CURLOPT_POST, 1L);
    curl_easy_setopt(easy, CURLOPT_POSTFIELDS, payload.c_str());
    curl_easy_setopt(easy, CURLOPT_POSTFIELDSIZE, payload.size());

    // Привязываем коллбэк записи и передаём ctx как userdata
    curl_easy_setopt(easy, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(easy, CURLOPT_WRITEDATA, ctx);
    curl_easy_setopt(easy, CURLOPT_PRIVATE, ctx);

    // Доп. настройки для неблокирующей работы
    curl_easy_setopt(easy, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(easy, CURLOPT_NOSIGNAL, 1L);

    // 4. Отдаём в multi handle
    curl_multi_add_handle(multi_handle, easy);
  } catch (const std::exception &e) {
    std::cerr << "An exception was occured while pasing client data: " << e.what() << std::endl;
    close(client->fd);
    web_clients.erase(web_clients.begin() + client_index);
  }
}

int main() {
  readAndWriteDataPack();
  int tcpfd = initTcp(PORT);
  std::cout << "Запущен сокет на порту " << PORT << std::endl;
  int webTcpFd = initTcp(WEB_PORT);
  std::cout << "Запущен веб сокет на порту " << WEB_PORT << std::endl;

  epoll_fd = epoll_create1(O_CLOEXEC);
  timer_fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);

  struct epoll_event ev;
  ev.events = EPOLLIN;
  ev.data.fd = tcpfd;
  epoll_ctl(epoll_fd, EPOLL_CTL_ADD, tcpfd, &ev);

  ev.events = EPOLLIN;
  ev.data.fd = webTcpFd;
  epoll_ctl(epoll_fd, EPOLL_CTL_ADD, webTcpFd, &ev);

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
        check_completed_requests();
      } else if (fd == webTcpFd) {
        int webclientfd = accept4(fd, NULL, NULL, SOCK_NONBLOCK | SOCK_CLOEXEC);
        if (webclientfd < 0) {
          perror("web client error");
          continue;
        }

        struct WebClient client;
        client.fd = webclientfd;
        std::cout << "Web clietn " << webclientfd << " connected" << std::endl;

        web_clients.push_back(client);

        ev.events = EPOLLIN;
        ev.data.fd = webclientfd;
        epoll_ctl(epoll_fd, EPOLL_CTL_ADD, webclientfd, &ev);
      } else {
        for (size_t i = 0; i < web_clients.size(); i++) {
          struct WebClient *client = &web_clients[i];
          if (fd == client->fd) {
            onWebClientUpdate(client, i);
            goto next;
          }
        }

        for (size_t i = 0; i < clients.size(); i++) {
          struct MinecraftClient *client = &clients[i];
          if (fd == client->fd) {
            onClientUpdate(client, i);
            goto next;
          }
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
        check_completed_requests();
      }

    next:;
    }
  }

  return 0;
}
