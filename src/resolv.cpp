#include "resolv.hpp"

#include <cstdio>
#include <cstring>
#include <netdb.h>
#include <sys/socket.h>

int resolve_host(const char *host, int port, ResolvedAddress *out) {
  struct addrinfo hints, *res;
  char port_str[6];
  snprintf(port_str, sizeof(port_str), "%d", port);

  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_UNSPEC;     // AF_UNSPEC = поддерживаем и IPv4, и IPv6!
  hints.ai_socktype = SOCK_STREAM; // TCP
  hints.ai_flags = AI_NUMERICHOST; // Запрашивать v6 только если в системе есть IPv6-интерфейсы

  int status = getaddrinfo(host, port_str, &hints, &res);
  if (status != 0) {
    fprintf(stderr, "Ошибка резолва %s: %s\n", host, gai_strerror(status));
    return -1;
  }

  // Сохраняем адрес и его размер
  memcpy(&out->addr, res->ai_addr, res->ai_addrlen);
  out->addr_len = res->ai_addrlen;
  out->family = res->ai_family;

  freeaddrinfo(res);
  return 0;
}
