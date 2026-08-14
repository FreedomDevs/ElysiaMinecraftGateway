#include "config.hpp"
#include <fcntl.h>
#include <spdlog/spdlog.h>
#include <sys/stat.h>
#include <unistd.h>

void Config::load_config_or_exit(const std::string &fileName) noexcept {
  SPDLOG_INFO("Инициализация конфиг файла");

st:;
  int filefd = open(fileName.c_str(), O_RDONLY);
  if (filefd >= 0) {
    struct stat st;
    if (fstat(filefd, &st) < 0) {
      close(filefd);

      SPDLOG_CRITICAL("Ошибка с конфиг файлом");
      exit(1);
    }

    ssize_t size = st.st_size;

    std::vector<char> buf(size);
    ssize_t n = read(filefd, buf.data(), buf.size());

    if (n < 0) {
      close(filefd);
      SPDLOG_CRITICAL("Ошибка с конфиг файлом");
      exit(1);
    }
    close(filefd);

    std::string jsonText(buf.begin(), buf.end());
    try {
      nlohmann::json j = nlohmann::json::parse(jsonText);

      gamePort = j["gamePort"];
      webPort = j["webPort"];
      checkRefreshTokenUrl = j["check_refresh_token_url"];
      refreshUrl = j["refresh_url"];

      for (auto &el : j["routes"].items()) {
        routes[el.key()].id_on_server = 0;
        routes[el.key()].id_used_to_ping = 0;

        for (auto &eq : el.value().items()) {
          if (eq.key() == "server")
            routes[el.key()].server = eq.value();
          else if (eq.key() == "id_on_server")
            routes[el.key()].id_on_server = eq.value();
          else if (eq.key() == "id_used_to_ping")
            routes[el.key()].id_used_to_ping = eq.value();
          else
            SPDLOG_WARN("Неиспользованный параметр конфига: {}", eq.key());
        }
      }

      for (auto &el : j["servers"].items()) {
        std::vector<struct Config::ConfigServer> servers;
        for (auto &eq : j["servers"][el.key()].items()) {
          Config::ConfigServer server;
          for (auto &eq1 : eq.value().items()) {
            if (eq1.key() == "port") {
              server.port = eq1.value();
            } else if (eq1.key() == "host") {
              server.host = eq1.value();
            }
          }

          servers.push_back(server);
        }

        this->servers[el.key()] = std::move(servers);
      }

      SPDLOG_INFO("Инициализация конфиг файла завершена!");

    } catch (const std::exception &e) {
      SPDLOG_CRITICAL("Ошибка при инициализации конфига {}", e.what());
      exit(1);
    }
  } else {
    SPDLOG_INFO("Конфиг файла нету создаём...");
    int newFilefd = open(fileName.c_str(), O_CREAT | O_WRONLY, 0644);
    if (newFilefd < 0) {
      return;
    }

    nlohmann::json j;
    j["gamePort"] = 25565;
    j["webPort"] = 8090;

    j["check_refresh_token_url"] = "https://fin1-services.elysiac.fun/auth/check_refresh_token";
    j["refresh_url"] = "https://fin1-services.elysiac.fun/auth/refresh";

    j["routes"]["localhost"] = {{"server", "Surv"}};
    j["servers"]["Surv"][0]["host"] = "localhost";
    j["servers"]["Surv"][0]["port"] = 25566;

    std::string data = j.dump(2);
    write(newFilefd, data.c_str(), data.size());
    close(newFilefd);
    goto st;
  }
}

struct Config::RouteConfig *Config::get_routeconfig_by_domain(const std::string &domain) noexcept {
  auto it = routes.find(domain);
  if (it != routes.end()) [[likely]]
    return &it->second;

  return NULL;
}

std::vector<struct Config::ConfigServer> *Config::get_server_by_name(const std::string &serverName) noexcept {
  auto it = servers.find(serverName);
  if (it != servers.end()) [[likely]]
    return &it->second;

  return NULL;
}
