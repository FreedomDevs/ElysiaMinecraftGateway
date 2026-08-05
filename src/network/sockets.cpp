#include "sockets.hpp"
#include "../epoll_fd.hpp"
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <spdlog/spdlog.h>
#include <sys/socket.h>
#include <sys/timerfd.h>
#include <sys/un.h>

int init_tcp_socket(uint16_t port, in6_addr addr) noexcept {
  int server_fd = socket(AF_INET6, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
  if (server_fd < 0) {
    SPDLOG_CRITICAL("TCP: Socket failed: {}", std::system_category().message(errno));
    exit(1);
  }

  struct sockaddr_in6 sockaddr;
  sockaddr.sin6_family = AF_INET6;
  sockaddr.sin6_port = htons(port);
  sockaddr.sin6_addr = addr;

  int reuse = 1;
  setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

  int bind_res = bind(server_fd, (const struct sockaddr *)&sockaddr, sizeof(sockaddr));
  if (bind_res < 0) {
    SPDLOG_CRITICAL("Не удалось прослушать порт: {}", std::system_category().message(errno));
    exit(1);
  }

  int qlen = 50;
  setsockopt(server_fd, SOL_TCP, TCP_FASTOPEN, &qlen, sizeof(qlen));

  int listen_res = listen(server_fd, SOMAXCONN);
  if (listen_res < 0) {
    SPDLOG_CRITICAL("Не удалось прослушать порт: {}", std::system_category().message(errno));
    exit(1);
  }

  ev.events = EPOLLIN;
  ev.data.fd = server_fd;
  epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_fd, &ev);

  return server_fd;
}

int init_timerfd() noexcept {
  int timerfd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
  if (timerfd == -1) {
    SPDLOG_CRITICAL("Cannot create timerfd for curl: ", std::system_category().message(errno));
    exit(1);
  }

  static struct epoll_event ev;
  ev.events = EPOLLIN;
  ev.data.fd = timerfd;
  epoll_ctl(epoll_fd, EPOLL_CTL_ADD, timerfd, &ev);

  return timerfd;
}
