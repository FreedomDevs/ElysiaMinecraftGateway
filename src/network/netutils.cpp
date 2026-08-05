#include "netutils.hpp"
#include <cstdint>
#include <spdlog/spdlog.h>
#include <unistd.h>

void read_timerfd(int fd) {
  static uint64_t expirations;
  if (read(fd, &expirations, sizeof(expirations)) == -1) {
    if (errno == EAGAIN) {
      SPDLOG_WARN("Пытались прочитать timerfd: {}, но он пуст", fd);
    } else {
      SPDLOG_CRITICAL("Произошла неизвестная ошибка в timerfd, {}: {}", fd, std::system_category().message(errno));
      exit(1);
    }
  }
}
