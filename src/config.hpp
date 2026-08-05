#pragma once
#include <flat_map>
#include <netinet/in.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <string>

class Config {
public:
  const std::string configFile;
  struct ConfigServer {
    in_port_t port;
    std::string host;
    std::string domain;
  };

  explicit Config(std::string fileName) : configFile(std::move(fileName)) { load_config_or_exit(); }

  uint16_t get_web_port() const noexcept { return webPort; }
  uint16_t get_game_port() const noexcept { return gamePort; }
  const std::string get_check_refresh_token_url() const noexcept { return checkRefreshTokenUrl; }
  const std::string get_refresh_url() const noexcept { return refreshUrl; }

  std::optional<std::string *> get_servername_by_domain(const std::string &domain) noexcept;
  std::optional<struct ConfigServer *> get_server_by_name(const std::string &serverName) noexcept;

private:
  std::flat_map<std::string, std::string> routes;
  std::flat_map<std::string, struct ConfigServer> servers;

  in_port_t webPort, gamePort;
  std::string checkRefreshTokenUrl, refreshUrl;

  void load_config_or_exit() noexcept;
};
