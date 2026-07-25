#pragma once
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdio.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

static inline int initTcp(uint16_t PORT) {
  int server_fd = socket(AF_INET6, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
  if (server_fd < 0) {
    perror("TCP: Socket failed");
    _exit(1);
  }

  struct sockaddr_in6 addr;
  addr.sin6_family = AF_INET6;
  addr.sin6_port = htons(PORT);
  addr.sin6_addr = in6addr_any;

  int reuse = 1;
  setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

  int bind_res = bind(server_fd, (const struct sockaddr *)&addr, sizeof(addr));
  if (bind_res < 0) {
    perror("Не удалось прослушать порт");
    _exit(1);
  }

  int qlen = 50;
  setsockopt(server_fd, SOL_TCP, TCP_FASTOPEN, &qlen, sizeof(qlen));

  int listen_res = listen(server_fd, SOMAXCONN);
  if (listen_res < 0) {
    perror("Не удалось прослушать порт");
    _exit(1);
  }

  return server_fd;
}
