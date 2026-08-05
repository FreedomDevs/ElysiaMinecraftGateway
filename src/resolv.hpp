#pragma once

#include <netdb.h>
#include <sys/socket.h>

typedef struct {
  struct sockaddr_storage addr; // Работает и под sockaddr_in, и под sockaddr_in6
  socklen_t addr_len;           // Длина (sizeof(sockaddr_in) или sizeof(sockaddr_in6))
  int family;                   // AF_INET или AF_INET6
} ResolvedAddress;

int resolve_host(const char *host, int port, ResolvedAddress *out);
