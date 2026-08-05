#pragma once
#include <bits/types/struct_itimerspec.h>
#include <cstdint>
#include <netinet/in.h>

int init_tcp_socket(uint16_t port, in6_addr addr = in6addr_any) noexcept;
int init_timerfd() noexcept;
