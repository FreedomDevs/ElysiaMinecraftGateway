#pragma once
#include <chrono>
#include <cstdint>
#include <spdlog/spdlog.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <unordered_map>

#include "readbuf.hpp"

namespace draining_sockets {
inline constexpr long timeout_secs = 10;

struct DrainingSocket {
  std::chrono::steady_clock::time_point deadline;
};
inline std::unordered_map<int, DrainingSocket> draining_sockets;

inline void drain(const int fd, const uint32_t event) {
  long ret = read(fd, temp_buf, sizeof(temp_buf));
  if (event & EPOLLHUP) {
    if (ret == sizeof(temp_buf))
      return;

    goto clean;
  }
  if (ret > 0)
    return;
  if (ret < 0) {
    SPDLOG_ERROR("[conn#{}] Ошибка при закрытии сокета клиента");
  }

clean:
  close(fd);
  draining_sockets.erase(fd);
  SPDLOG_INFO("[conn#{}] Сокет закрыт");
}
inline void add(const int fd) {
  shutdown(fd, SHUT_WR);
  auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(timeout_secs);
  draining_sockets[fd] = DrainingSocket{deadline};
}
inline bool has(const int fd) { return draining_sockets.contains(fd); }

inline void clean() {
  auto now = std::chrono::steady_clock::now();

  for (auto it = draining_sockets.begin(); it != draining_sockets.end();) {
    int fd = it->first;
    const auto &info = it->second;

    if (now >= info.deadline) {
      close(fd);
      it = draining_sockets.erase(it);
      SPDLOG_INFO("[conn#{}] Сокет закрыт по timeout в {} секунд", fd, timeout_secs);
    } else {
      ++it;
    }
  }
}
} // namespace draining_sockets
