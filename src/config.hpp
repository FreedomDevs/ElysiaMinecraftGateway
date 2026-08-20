#pragma once
#include <cstddef>
#include <cstdint>
#include <flat_map>
#include <netinet/in.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <string>

class Config {
public:
  struct ConfigServer {
    std::string host;
    in_port_t port;
  };
  struct RouteConfig {
    std::string server;
    int id_on_server;
    int id_used_to_ping;
    size_t statid;
  };

  uint16_t get_web_port() const noexcept { return webPort; }
  uint16_t get_game_port() const noexcept { return gamePort; }
  const std::string get_check_refresh_token_url() const noexcept { return checkRefreshTokenUrl; }
  const std::string get_refresh_url() const noexcept { return refreshUrl; }

  struct RouteConfig *get_routeconfig_by_domain(const std::string &domain) noexcept;
  std::vector<struct ConfigServer> *get_server_by_name(const std::string &serverName) noexcept;

  void load_config_or_exit(const std::string &fileName) noexcept;

private:
  std::flat_map<std::string, struct RouteConfig> routes;
  std::flat_map<std::string, std::vector<struct ConfigServer>> servers;

  in_port_t webPort, gamePort;
  std::string checkRefreshTokenUrl, refreshUrl;
};
