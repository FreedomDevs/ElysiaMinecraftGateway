#include "config.hpp"
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

void Config::load_config_or_exit() noexcept {
  SPDLOG_INFO("Инициализация конфиг файла");

st:;
  int filefd = open(configFile.c_str(), O_RDONLY);
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
        routes[el.key()] = el.value();
      }

      for (auto &el : j["servers"].items()) {
        Config::ConfigServer server;
        for (auto &eq : j["servers"][el.key()].items()) {
          if (eq.key() == "port") {
            server.port = eq.value();
          } else if (eq.key() == "host") {
            server.host = eq.value();
          }
        }

        servers[el.key()] = server;
      }

      SPDLOG_INFO("Инициализация конфиг файла завершена!");

    } catch (const std::exception &e) {
      SPDLOG_CRITICAL("Ошибка при инициализации конфига {}", e.what());
      exit(1);
    }
  } else {
    SPDLOG_INFO("Конфиг файла нету создаём...");
    int newFilefd = open(configFile.c_str(), O_CREAT | O_WRONLY, 0644);
    if (newFilefd < 0) {
      return;
    }

    nlohmann::json j;
    j["gamePort"] = 25565;
    j["webPort"] = 8090;

    j["check_refresh_token_url"] = "https://fin1-services.elysiac.fun/auth/check_refresh_token";
    j["refresh_url"] = "https://fin1-services.elysiac.fun/auth/refresh";

    j["routes"]["localhost"] = "Surv";
    j["servers"]["Surv"]["host"] = "localhost";
    j["servers"]["Surv"]["port"] = 25566;

    std::string data = j.dump(2);
    write(newFilefd, data.c_str(), data.size());
    close(newFilefd);
    goto st;
  }
}

std::optional<std::string *> Config::get_servername_by_domain(const std::string &domain) noexcept {
  auto it = routes.find(domain);
  if (it != routes.end()) [[likely]]
    return &it->second;

  return std::nullopt;
}

std::optional<struct Config::ConfigServer *> Config::get_server_by_name(const std::string &serverName) noexcept {
  auto it = servers.find(serverName);
  if (it != servers.end()) [[likely]]
    return &it->second;

  return std::nullopt;
}
