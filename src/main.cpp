#include "config.hpp"
#include "embed.hpp"
#include "network/PacketWriter.hpp"
#include "network/netutils.hpp"
#include "network/packets/Handshake.hpp"
#include "network/packets/configuration/StoreCookie.hpp"
#include "network/packets/configuration/Transfer.hpp"
#include "network/packets/login/LoginAcknowledged.hpp"
#include "network/packets/login/LoginStart.hpp"
#include "network/packets/login/LoginSuccess.hpp"
#include "network/packets/status/StatusPing.hpp"
#include "network/packets/status/StatusPong.hpp"
#include "network/packets/status/StatusRequest.hpp"
#include <arpa/inet.h>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <curl/multi.h>
#include <exception>
#include <fcntl.h>
#include <iostream>
#include <netdb.h>
#include <netinet/in.h>
#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>
#include <optional>
#include <span>
#include <spanstream>
#include <spdlog/pattern_formatter.h>
#include <spdlog/spdlog.h>
#include <sstream>
#include <stdexcept>
#include <string>
#include <sys/epoll.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/timerfd.h>
#include <sys/uio.h>
#include <unistd.h>
#include <vector>

#include "epoll_fd.hpp"
#include "network/curl_init.hpp"
#include "network/sockets.hpp"
#include "resolv.hpp"

Config config = Config("config.json");
static iovec iov[4]; // Нужен 4

enum class ConnectionState : unsigned char { HandShake, Status, Login, Configuration, Play, Died };

enum class StatusReadState : unsigned char { Connection, Read };

static char temp_buf[2048];

static int last_connid = 0;

struct MinecraftClient {
  int fd;
  uint32_t connid;
  int protocol_version;
  int statusread_fd;
  std::vector<unsigned char> sendbuf;
  StatusReadState statusread_state;
  ConnectionState state;
  in6_addr addr;
  std::string username;
  std::string *routed_server;
  Config::ConfigServer *routed_server_config;
  bool send_dialog;
  std::vector<unsigned char> buf;
};

struct WebClient {
  int fd;
  uint32_t connid;
};

struct RequestContext {
  std::string token;
  std::string payload;
  std::string response_body;
  std::string serverName;
  bool is_check_refresh;
  int res_fd;
  struct curl_slist *headers = nullptr;

  ~RequestContext() {
    if (headers) {
      curl_slist_free_all(headers);
    }
  }
};

struct PlayerSession {
  in6_addr ip;
  std::string refresh_token;
  std::chrono::system_clock::time_point expires_at; // Время истечения токена
};

constexpr uint8_t keepalivepacket[] = {0x09, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
constexpr uint8_t status_request_packet[] = {0x01, 0x00};
static struct itimerspec minecraft_keepalive_ts = {};
int minecraft_keepalive_timerfd;
int tcpfd;
int webTcpFd;

char ipv6_str[INET6_ADDRSTRLEN];
static sockaddr_in6 sockaddr_addr{};
static socklen_t sockaddr_len = sizeof(sockaddr_in6);

std::vector<MinecraftClient> clients;
std::vector<WebClient> web_clients;

std::string sanitize_for_log(std::string_view input) {
  std::string clean;
  clean.reserve(input.size());
  for (char c : input) {
    // Оставляем только обычные printable ASCII символы
    if (std::isprint(static_cast<unsigned char>(c))) {
      clean += c;
    } else {
      // Непечатаемые символы и ESC (0x1B) превращаем в hex-escaped вид
      clean += fmt::format("\\x{:02x}", static_cast<unsigned char>(c));
    }
  }
  return clean;
}

static inline void setMinecraftKeepaliveTimerstate(bool state) {
  if (state) {
    if (minecraft_keepalive_ts.it_interval.tv_sec == 0) {
      minecraft_keepalive_ts.it_value.tv_sec = 19;
      minecraft_keepalive_ts.it_interval.tv_sec = 19;
      timerfd_settime(minecraft_keepalive_timerfd, 0, &minecraft_keepalive_ts, NULL);
      SPDLOG_INFO("Таймер на keepalive был активирован");
    }
  } else {
    if (minecraft_keepalive_ts.it_interval.tv_sec == 19) {
      minecraft_keepalive_ts.it_value.tv_sec = 0;
      minecraft_keepalive_ts.it_interval.tv_sec = 0;
      timerfd_settime(minecraft_keepalive_timerfd, 0, &minecraft_keepalive_ts, NULL);
      SPDLOG_INFO("Таймер на keepalive был отключен");
    }
  }
}

class TokenStorage {
private:
  // username (в нижнем регистре) -> Данные сессии
  std::unordered_map<std::string, PlayerSession> sessions;

  // Никнеймы в Minecraft нечувствительны к регистру (Player == player)
  static std::string to_lower(std::string str) {
    std::transform(str.begin(), str.end(), str.begin(), [](unsigned char c) { return std::tolower(c); });
    return str;
  }

public:
  // Сохранить или обновить токен игрока
  void save_token(const std::string &username, in6_addr ip, const std::string &token, std::chrono::seconds ttl) {

    PlayerSession session{ip, token, std::chrono::system_clock::now() + ttl};

    sessions[to_lower(username)] = std::move(session);
  }

  // Получить и проверить токен
  std::optional<PlayerSession> get_valid_session(const std::string &username, in6_addr current_ip) {
    auto key = to_lower(username);
    auto it = sessions.find(key);

    if (it == sessions.end()) {
      return std::nullopt; // Игрок не найден
    }

    const auto &session = it->second;

    inet_ntop(AF_INET6, &current_ip, ipv6_str, sizeof(ipv6_str));
    inet_ntop(AF_INET6, &session.ip, ipv6_str, sizeof(ipv6_str));

    // 1. Проверяем совпадение IP
    if (!IN6_ARE_ADDR_EQUAL(&session.ip, &current_ip)) {
      return std::nullopt; // IP изменился (возможная подмена)
    }

    // 2. Проверяем срок годности токена
    if (std::chrono::system_clock::now() > session.expires_at) {
      sessions.erase(it); // Токен просрочен, удаляем
      return std::nullopt;
    }

    return session;
  }

  // Удалить токен (например, при выходе/разлогине)
  void revoke_token(const std::string &username) { sessions.erase(to_lower(username)); }
};
TokenStorage storage;

size_t write_callback(char *ptr, size_t size, size_t nmemb, void *userdata) {
  size_t total_size = size * nmemb;
  auto *ctx = static_cast<RequestContext *>(userdata);

  // Дописываем пришедшие байты в наш response_body
  ctx->response_body.append(ptr, total_size);

  return total_size;
}

static inline void routePlayer(const std::string &refresh_token, MinecraftClient *client) {
  CURL *easy = curl_easy_init();
  if (!easy)
    return;

  // 1. Создаём контекст в куче
  auto *ctx = new RequestContext();
  ctx->is_check_refresh = false;
  ctx->serverName = *client->routed_server;
  ctx->res_fd = client->fd;

  // 2. Формируем заголовки (если нужно)
  ctx->headers = curl_slist_append(ctx->headers, "Content-Type: application/json");

  int out_len = 0;
  char *decoded = curl_easy_unescape(multi_handle, refresh_token.c_str(), refresh_token.length(), &out_len);
  std::string clean_token(decoded, out_len);
  curl_free(decoded);

  nlohmann::json body = {{"refresh_token", clean_token}, {"serverName", *client->routed_server}};

  // 3. Настраиваем curl easy handle
  curl_easy_setopt(easy, CURLOPT_URL, config.get_refresh_url().c_str());
  curl_easy_setopt(easy, CURLOPT_HTTPHEADER, ctx->headers);

  ctx->payload = body.dump();
  curl_easy_setopt(easy, CURLOPT_POST, 1L);
  curl_easy_setopt(easy, CURLOPT_POSTFIELDS, ctx->payload.c_str());
  curl_easy_setopt(easy, CURLOPT_POSTFIELDSIZE, ctx->payload.size());

  // Привязываем коллбэк записи и передаём ctx как userdata
  curl_easy_setopt(easy, CURLOPT_WRITEFUNCTION, write_callback);
  curl_easy_setopt(easy, CURLOPT_WRITEDATA, ctx);
  curl_easy_setopt(easy, CURLOPT_PRIVATE, ctx);

  // Доп. настройки для неблокирующей работы
  curl_easy_setopt(easy, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(easy, CURLOPT_NOSIGNAL, 1L);

  // 4. Отдаём в multi handle
  curl_multi_add_handle(multi_handle, easy);
}

static inline void onPacket(MinecraftClient *client, PacketReader &packet) {
#define KILL_CONN                                                                                                                          \
  do {                                                                                                                                     \
    client->state = ConnectionState::Died;                                                                                                 \
    return;                                                                                                                                \
  } while (0)

  if (client->state == ConnectionState::HandShake) {
    if (packet.getPacketId() != 0) {
      SPDLOG_INFO("[conn#{} client#{}] Клиент прислал HandShake с некорректным id: {}", client->fd, client->connid, packet.getPacketId());
      KILL_CONN;
    }
    HandShake handshake;
    handshake.decode(packet);

    std::string clean_address = sanitize_for_log(handshake.getServerAddress());
    SPDLOG_INFO("[conn#{} client#{}] Получен Handshake, адрес: {}, версия протокола: {}, причина подключения: {}", client->fd,
                client->connid, clean_address, handshake.getProtocolVersion(), (int)handshake.getConnectionReason());

    if (handshake.getServerPort() != config.get_game_port()) {
      SPDLOG_WARN("[conn#{} client#{}] Некорректный порт для подключения, ожидался: {}, получен: {}", client->fd, client->connid,
                  std::to_string(config.get_game_port()), std::to_string(handshake.getServerPort()));
      KILL_CONN;
    }

    auto server_name = config.get_servername_by_domain(handshake.getServerAddress());
    if (!server_name.has_value()) {
      SPDLOG_WARN("[conn#{} client#{}] Не найден домен для подключения пользователя: {}", client->fd, client->connid, clean_address);
      KILL_CONN;
    }

    auto server_config = config.get_server_by_name(*server_name.value());
    if (!server_config.has_value()) [[unlikely]] {
      SPDLOG_ERROR("[conn#{} client#{}] Не найден сервер для подключения пользователя: {}", client->fd, client->connid,
                   *client->routed_server);
      KILL_CONN;
    }

    client->routed_server = server_name.value();
    client->routed_server_config = server_config.value();
    client->protocol_version = handshake.getProtocolVersion();

    if (handshake.getConnectionReason() == ConnectionReason::Status)
      client->state = ConnectionState::Status;
    else if (handshake.getConnectionReason() == ConnectionReason::Connnect)
      client->state = ConnectionState::Login;
    else {
      SPDLOG_ERROR("[conn#{} client#{}] Получен некорректный ConnectionReason", client->fd, client->connid);
      KILL_CONN;
    }
  } else if (client->state == ConnectionState::Status) {
    if (packet.getPacketId() == 0) {
      StatusRequest req;
      req.decode(packet);

      SPDLOG_INFO("[conn#{} client#{}] Получен запрос Status", client->fd, client->connid);

      ResolvedAddress res_addr;
      if (resolve_host(client->routed_server_config->host.c_str(), client->routed_server_config->port, &res_addr) < 0) {
        SPDLOG_ERROR("[conn#{} client#{}] Не удалось отрезолвить домен в адресе сервера", client->fd, client->connid);
        KILL_CONN;
      }

      int sock_fd = socket(res_addr.family, SOCK_STREAM | SOCK_NONBLOCK, 0);
      if (sock_fd < 0) {
        SPDLOG_ERROR("[conn#{} client#{}] Не удалось создать сетевой сокет", client->fd, client->connid,
                     std::system_category().message(errno));
        KILL_CONN;
      }

      SPDLOG_DEBUG("[conn#{} client#{}] Создан statusfd: {}", client->fd, client->connid, sock_fd);

      client->statusread_fd = sock_fd;
      client->statusread_state = StatusReadState::Connection;

      int res = connect(sock_fd, (struct sockaddr *)&res_addr.addr, res_addr.addr_len);
      if (res < 0 && errno != EINPROGRESS) {
        SPDLOG_DEBUG("[conn#{} client#{}] Не удалось создать соединение с сервером: {}", client->fd, client->connid,
                     std::system_category().message(errno));
        KILL_CONN;
      }

      struct epoll_event ev;
      memset(&ev, 0, sizeof(ev));

      // Настраиваем epoll на ожидание готовности к ЗАПИСИ (EPOLLOUT)
      ev.events = EPOLLOUT | EPOLLONESHOT;
      ev.data.fd = sock_fd;

      if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, sock_fd, &ev) < 0) {
        SPDLOG_DEBUG("[conn#{} client#{}] Не удалось добавить backend сервер в epoll: {}", client->fd, client->connid,
                     std::system_category().message(errno));
        KILL_CONN;
      }
    } else if (packet.getPacketId() == 1) {
      StatusPing req;
      req.decode(packet);

      SPDLOG_DEBUG("[conn#{} client#{}] Получен Ping запрос от клиента", client - fd, client->connid);

      PacketWriter writer = StatusPong::encode(req.getPayload());
      writer.generate_iovec(iov);

      msghdr msg = {};
      msg.msg_iov = iov;
      msg.msg_iovlen = 2;

      sendmsg(client->fd, &msg, MSG_MORE);
      close(client->fd);
      client->fd = -1;

      client->state = ConnectionState::Died;
      SPDLOG_INFO("[conn#{} client#{}] Вернули Pong клиенту, и закрыли соединение", client->fd, client->connid);
    } else {
      SPDLOG_INFO("[conn#{} client#{}] Получен status пакет с некорректным id: {}", client->fd, client->connid, packet.getPacketId());
      KILL_CONN;
    }
  } else if (client->state == ConnectionState::Login) {
    if (packet.getPacketId() == 0) {
      LoginStart req;
      req.decode(packet);

      LoginSuccess resp;
      resp.encode(req.getUUID1(), req.getUUID2(), req.getUsername());

      PacketWriter writer = resp.getPacketWriter();
      writer.generate_iovec(iov);
      writev(client->fd, iov, 2);

      client->username = req.getUsername();
    } else if (packet.getPacketId() == 3) {
      LoginAcknowledged res;
      res.decode(packet);

      if (client->username.empty()) {
        throw std::runtime_error("Client idet v pered paravoza");
      }

      client->state = ConnectionState::Configuration;

      std::optional<PlayerSession> session = storage.get_valid_session(client->username, client->addr);
      if (session.has_value()) {
        routePlayer(session->refresh_token, client);
      } else {
        client->send_dialog = true;
        write(client->fd, embedded::dialog.data(), embedded::dialog.size());
      }
    } else {
      SPDLOG_INFO("[conn#{} client#{}] Получен login пакет с некорректным id: {}", client->fd, client->connid, packet.getPacketId());
      KILL_CONN;
    }
  } else if (client->state == ConnectionState::Configuration) {
    if (packet.getPacketId() == 0 || packet.getPacketId() == 2 || packet.getPacketId() == 4) {
      return;
    } else if (packet.getPacketId() == 8) {
      SPDLOG_INFO("Client closed connection");
      close(client->fd);
      client->state = ConnectionState::Died;
    } else {
      throw std::runtime_error("Incorrect packet id " + std::to_string(packet.getPacketId()));
    }
  } else {
    SPDLOG_INFO("[conn#{} client#{}] Получен configuration пакет с некорректным id: {}", client->fd, client->connid, packet.getPacketId());
    KILL_CONN;
  }

#undef KILL_CONN
}

static inline void closeConns(MinecraftClient *client, size_t client_index) {
  if (client->fd >= 0) {
    close(client->fd);
    SPDLOG_INFO("[conn#{} client#{}] Соединение закрыто", client->fd, client->connid);
  }
  if (client->statusread_fd >= 0) {
    close(client->statusread_fd);
    SPDLOG_INFO("[conn#{} client#{}] Соединение с backend сервером закрыто, fd = {}", client->fd, client->connid, client->statusread_fd);
  }

  setMinecraftKeepaliveTimerstate(false);
  clients.erase(clients.begin() + client_index);
}

static inline void onClientUpdate(MinecraftClient *client, size_t client_index) {
  long ret = read(client->fd, temp_buf, sizeof(temp_buf));
  if (ret < 0 && errno != EAGAIN) [[unlikely]] {
    SPDLOG_ERROR("[conn#{} client#{}] Не удалось вычитать данные клиента: {}", client->fd, client->connid,
                 std::system_category().message(errno));
    closeConns(client, client_index);
    return;
  }

  client->buf.insert(client->buf.end(), temp_buf, temp_buf + ret);
  try {
    while (true) {
      std::span<unsigned char> view(client->buf.data(), client->buf.size());
      PacketReader reader(view);

      std::optional<size_t> ret =
          client->state == ConnectionState::HandShake ? reader.readUncompressed(260) : reader.readUncompressed(1000);
      if (!ret.has_value())
        break;

      onPacket(client, reader);

      if (client->state == ConnectionState::Died) {
        closeConns(client, client_index);
        return;
      }

      memmove(client->buf.data(), client->buf.data() + *ret, client->buf.size() - *ret);
      client->buf.resize(client->buf.size() - *ret);
    }
  } catch (const std::exception &e) {
    SPDLOG_ERROR("[conn#{} client#{}] Неожиданная ошибка произошла при парсинга данных клиента: {}", client->fd, client->connid, e.what());
    closeConns(client, client_index);
  }

  if (ret == 0) {
    SPDLOG_WARN("[conn#{} client#{}] Клиент закрыл соединение, байт осталось необработанно: {}", client->fd, client->connid,
                client->buf.size());
    closeConns(client, client_index);
    return;
  }
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
          try {
            // Парсим строку в объект json
            nlohmann::json data = nlohmann::json::parse(ctx->response_body);

            std::string username = data.at("data").at("username");

            auto client = clients.end();
            for (auto i = clients.begin(); i < clients.end(); i++) {
              if (i->username == username) {
                client = i;
                break;
              }
            }

            if (client == clients.end()) {
              send(ctx->res_fd, embedded::not_ok_html.data(), embedded::not_ok_html.size(), MSG_MORE);
              close(ctx->res_fd);
              goto clean;
            }

            storage.save_token(username, client->addr, ctx->token, std::chrono::seconds(30 * 24 * 60 * 60));
            routePlayer(ctx->token, &*client);
          } catch (const std::exception &e) {
            std::cerr << "Ошибка парсинга JSON: " << e.what() << std::endl;

            send(ctx->res_fd, embedded::not_ok_html.data(), embedded::not_ok_html.size(), MSG_MORE);
            close(ctx->res_fd);
            goto clean;
          }

          send(ctx->res_fd, embedded::ok_html.data(), embedded::ok_html.size(), MSG_MORE);
          close(ctx->res_fd);
        clean:
          for (auto i = web_clients.begin(); i < web_clients.end(); i++) {
            if (i->fd == ctx->res_fd) {
              web_clients.erase(i);
              break;
            }
          }
        } else {
          try {
            // Парсим строку в объект json
            nlohmann::json data = nlohmann::json::parse(ctx->response_body);

            std::string token = data.at("data").at("token");

            auto client = clients.end();
            for (auto i = clients.begin(); i < clients.end(); i++) {
              if (i->fd == ctx->res_fd) {
                client = i;
                break;
              }
            }

            if (client == clients.end()) {
              close(ctx->res_fd);
              goto clean1;
            }

            PacketWriter packetwriter1 = StoreCookie::encode("eauth:eauth-jwt", token);
            packetwriter1.generate_iovec(iov);
            PacketWriter packetwriter2 = Transfer::encode(client->routed_server_config->host, client->routed_server_config->port);
            packetwriter2.generate_iovec_to_2(iov + 2);

            struct msghdr msg{};
            msg.msg_iov = iov;
            msg.msg_iovlen = 4; // 4 io мы использовали
            sendmsg(client->fd, &msg, MSG_MORE);
            close(client->fd);
          } catch (const std::exception &e) {
            std::cerr << "Ошибка парсинга JSON: " << e.what() << std::endl;

            for (auto i = clients.begin(); i < clients.end(); i++) {
              if (i->fd == ctx->res_fd) {
                storage.revoke_token(i->username);
              }
            }
            close(ctx->res_fd);
            goto clean1;
          }
        clean1:
          for (size_t i = 0; i < clients.size(); i++) {
            if (clients[i].fd == ctx->res_fd) {
              closeConns(&clients[i], i);
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

static inline void onWebClientUpdate(WebClient *client, size_t client_index) {
  std::string token;

  long ret;
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
      std::cout << "Invalid request" << std::endl;
      close(client->fd);
      web_clients.erase(web_clients.begin() + client_index);
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
      send(client->fd, embedded::not_ok_html.data(), embedded::not_ok_html.size(), MSG_MORE);
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

    int out_len = 0;
    char *decoded = curl_easy_unescape(multi_handle, token.c_str(), token.length(), &out_len);
    std::string clean_token(decoded, out_len);
    curl_free(decoded);

    nlohmann::json body = {{"refresh_token", clean_token}, {"request_username", true}};

    // 3. Настраиваем curl easy handle
    curl_easy_setopt(easy, CURLOPT_URL, config.get_check_refresh_token_url().c_str());
    curl_easy_setopt(easy, CURLOPT_HTTPHEADER, ctx->headers);

    ctx->payload = body.dump();
    curl_easy_setopt(easy, CURLOPT_POST, 1L);
    curl_easy_setopt(easy, CURLOPT_POSTFIELDS, ctx->payload.c_str());
    curl_easy_setopt(easy, CURLOPT_POSTFIELDSIZE, ctx->payload.size());

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
    std::cerr << "An exception occured while pasing client data: " << e.what() << std::endl;
    close(client->fd);
    web_clients.erase(web_clients.begin() + client_index);
  }
}

void onEpoll(epoll_event *ep_event) {
  int fd = ep_event->data.fd;
  uint32_t event = ep_event->events;

  if (fd == tcpfd) {
    struct MinecraftClient client;

    client.fd = accept4(fd, (sockaddr *)&sockaddr_addr, &sockaddr_len, SOCK_NONBLOCK | SOCK_CLOEXEC);
    if (client.fd < 0) {
      SPDLOG_ERROR("Can't accept minecraft connection: {}", std::system_category().message(errno));
      return;
    }

    client.state = ConnectionState::HandShake;
    client.statusread_fd = -1;
    client.connid = last_connid++;
    client.addr = sockaddr_addr.sin6_addr;

    inet_ntop(AF_INET6, &client.addr, ipv6_str, sizeof(ipv6_str));
    SPDLOG_INFO("[conn#{}, client#{}] Minecraft client connected: {}", client.fd, client.connid, ipv6_str);
    clients.push_back(client);

    ev.events = EPOLLIN;
    ev.data.fd = client.fd;
    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client.fd, &ev);
  } else if (fd == webTcpFd) {
    struct WebClient client;
    client.fd = accept4(fd, (sockaddr *)&sockaddr_addr, &sockaddr_len, SOCK_NONBLOCK | SOCK_CLOEXEC);
    if (client.fd < 0) {
      SPDLOG_ERROR("Can't accept web connection: ", std::system_category().message(errno));
      return;
    }

    client.connid = last_connid++;

    SPDLOG_INFO("[conn#{}, client#{}] Web client connected", client.fd, client.connid);
    web_clients.push_back(client);

    ev.events = EPOLLIN;
    ev.data.fd = client.fd;
    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client.fd, &ev);
  } else if (fd == curl_timer_fd) {
    read_timerfd(curl_timer_fd);
    int running_handles = 0;

    curl_multi_socket_action(multi_handle, CURL_SOCKET_TIMEOUT, 0, &running_handles);
    check_completed_requests();
  } else if (fd == minecraft_keepalive_timerfd) {
    read_timerfd(minecraft_keepalive_timerfd);

    for (auto &client : clients) {
      if (client.state == ConnectionState::Configuration) {
        ssize_t ret = write(client.fd, keepalivepacket, sizeof(keepalivepacket));
        if (ret != sizeof(keepalivepacket)) { // -1 или меньше чем sizeof(keepalivepacket)
          close(client.fd);
          client.state = ConnectionState::Died;
        }
      }
    }

    std::erase_if(clients, [](const MinecraftClient &c) { return c.state == ConnectionState::Died; });
  } else {
    for (size_t i = 0; i < web_clients.size(); i++) {
      struct WebClient *client = &web_clients[i];
      if (fd == client->fd) {
        onWebClientUpdate(client, i);
        return;
      }
    }

    for (size_t i = 0; i < clients.size(); i++) {
      struct MinecraftClient *client = &clients[i];
      if (fd == client->fd) {
        onClientUpdate(client, i);
        return;
      }

      if (fd == client->statusread_fd && client->statusread_state == StatusReadState::Connection) {
        int err = 0;
        socklen_t len = sizeof(err);
        if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len) < 0 || err != 0) {
          SPDLOG_ERROR("[conn#{} client#{}] Не удалось установить соединение с backend сервером: fd = {}, server = {}, errno = {}",
                       client->fd, client->connid, client->statusread_fd, *client->routed_server, std::system_category().message(errno));
          closeConns(client, i);
          return;
        }

        PacketWriter handshake = HandShake::encode(client->protocol_version, client->routed_server_config->host,
                                                   client->routed_server_config->port, ConnectionReason::Status);
        handshake.generate_iovec(iov);
        iov[2].iov_base = (void *)status_request_packet;
        iov[2].iov_len = sizeof(status_request_packet);

        msghdr msg = {};
        msg.msg_iov = iov;
        msg.msg_iovlen = 3;

        SPDLOG_INFO("conn#{} client#{}] Отправляем handshake на сервер: fd = {}", client->fd, client->connid, client->statusread_fd);
        sendmsg(client->statusread_fd, &msg, MSG_MORE);
        shutdown(client->statusread_fd, SHUT_WR);

        ev.events = EPOLLIN;
        ev.data.fd = client->statusread_fd;
        if (epoll_ctl(epoll_fd, EPOLL_CTL_MOD, client->statusread_fd, &ev) < 0) {
          SPDLOG_ERROR("[conn#{} client#{}] Не удалось переключить epoll в режим чтения на backend сервере: fd = {}, errno = {}",
                       client->fd, client->connid, client->statusread_fd, std::system_category().message(errno));
          closeConns(client, i);
          return;
        }

        client->statusread_state = StatusReadState::Read;
        return;
      }

      if (fd == client->statusread_fd && client->statusread_state == StatusReadState::Read) {
        long ret;
        while ((ret = read(client->statusread_fd, temp_buf, sizeof(temp_buf))) > 0) {
          client->sendbuf.insert(client->sendbuf.end(), temp_buf, temp_buf + ret);
        };

        if (ret < 0 and errno != EAGAIN) {
          SPDLOG_ERROR("[conn#{} client#{}] Не удалось прочитать данные от backend сервера: fd = {}, server = {}, errno {}", client->fd,
                       client->connid, client->statusread_fd, *client->routed_server, std::system_category().message(errno));
          closeConns(client, i);
          return;
        }

        try {
          std::span<unsigned char> view(client->sendbuf.data(), client->sendbuf.size());
          PacketReader reader(view);

          std::optional<size_t> ret = reader.readUncompressed();
          if (!ret.has_value())
            return;

          if (reader.getPacketId() != 0 || *ret != client->sendbuf.size()) {
            SPDLOG_ERROR("[conn#{} client#{}] Backend сервер вернул пакет с некорректным id: {}, или не удалось дочитать пакет до конца: "
                         "{}, fd = {}",
                         client->fd, client->connid, reader.getPacketId(), client->sendbuf.size(), client->statusread_fd);
            closeConns(client, i);
            return;
          }

          close(client->statusread_fd);
          SPDLOG_INFO("[conn#{} client#{}] Закрыто соединение с backend после успешного чтения статуса, fd = {}", client->fd,
                      client->connid, client->statusread_fd);

          write(client->fd, client->sendbuf.data(), client->sendbuf.size());

          client->statusread_fd = -1;
          client->sendbuf.clear();
          client->sendbuf.shrink_to_fit();
        } catch (const std::exception &e) {
          SPDLOG_ERROR("[conn#{} client#{}] An exception occured while pasing minecraft backend data: {}, {}", client->fd, client->connid,
                       *client->routed_server, e.what());
          closeConns(client, i);
          return;
        }

        if (ret == 0) {
          SPDLOG_ERROR("[conn#{} client#{}] Backend minecraft server unexpected closed connection: {}, {}", client->fd, client->connid,
                       *client->routed_server, std::system_category().message(errno));
          closeConns(client, i);
          return;
        }

        return;
      }
    }

    int action_mask = 0;
    if (event & EPOLLIN)
      action_mask |= CURL_CSELECT_IN;
    if (event & EPOLLOUT)
      action_mask |= CURL_CSELECT_OUT;
    if (event & (EPOLLERR | EPOLLHUP))
      action_mask |= CURL_CSELECT_ERR;

    int running_handles = 0;
    curl_multi_socket_action(multi_handle, fd, action_mask, &running_handles);

    check_completed_requests();
  }
}

class custom_level_formatter : public spdlog::custom_flag_formatter {
public:
  void format(const spdlog::details::log_msg &msg, const std::tm &, spdlog::memory_buf_t &dest) override {
    spdlog::string_view_t level_name;

    if (msg.level == spdlog::level::warn) {
      level_name = "warn";
    } else {
      level_name = spdlog::level::to_string_view(msg.level);
    }

    dest.append(level_name.data(), level_name.data() + level_name.size());
  }

  std::unique_ptr<custom_flag_formatter> clone() const override { return spdlog::details::make_unique<custom_level_formatter>(); }
};

int main() {
  auto formatter = std::make_unique<spdlog::pattern_formatter>();
  formatter->add_flag<custom_level_formatter>('u');
  formatter->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%u%$] %v");
  spdlog::set_formatter(std::move(formatter));

  std::signal(SIGPIPE, SIG_IGN);
  epoll_fd = epoll_create1(O_CLOEXEC);

  tcpfd = init_tcp_socket(config.get_game_port());
  SPDLOG_INFO("Запущен сокет на порту: {}", config.get_game_port());
  webTcpFd = init_tcp_socket(config.get_web_port());
  SPDLOG_INFO("Запущен веб сервер на порту: {}", config.get_web_port());
  minecraft_keepalive_timerfd = init_timerfd();
  init_curl_or_exit();

  struct epoll_event events[32];
  while (1) {
    int count = epoll_wait(epoll_fd, events, 32, -1);
    for (int i = 0; i < count; i++)
      onEpoll(events + i);
  }

  return 0;
}
