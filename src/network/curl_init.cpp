#include <spdlog/spdlog.h>
#include <sys/epoll.h>
#include <sys/timerfd.h>

#include "curl_init.hpp"
#include "sockets.hpp"

#include "../epoll_fd.hpp"

int cb_socket_action(CURL * /*easy*/, curl_socket_t s, int action, void * /*userp*/, void *socketp) {
  epoll_event ev{};
  ev.data.fd = s;

  if (action == CURL_POLL_IN) {
    ev.events = EPOLLIN;
  } else if (action == CURL_POLL_OUT) {
    ev.events = EPOLLOUT;
  } else if (action == CURL_POLL_INOUT) {
    ev.events = EPOLLIN | EPOLLOUT;
  } else if (action == CURL_POLL_REMOVE) {
    // Удаляем сокет из epoll
    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, s, nullptr);
    return 0;
  }

  // Если сокет уже регистрировался ранее (socketp != nullptr), делаем MOD, иначе ADD
  int op = (socketp != nullptr) ? EPOLL_CTL_MOD : EPOLL_CTL_ADD;
  epoll_ctl(epoll_fd, op, s, &ev);

  // Сохраняем маркер, что сокет зарегистрирован
  curl_multi_assign(multi_handle, s, (void *)1);

  return 0;
}

int cb_timer_action(CURLM * /*multi*/, long timeout_ms, void * /*userp*/) {
  struct itimerspec its{};

  if (timeout_ms < 0) {
    // timeout_ms < 0 означает, что curl просит отменить текущий таймер
    // Передаем нулевой its — это выключает timerfd
    timerfd_settime(curl_timer_fd, 0, &its, nullptr);

  } else {
    // Если curl просит 0 мс, взводим на 1 нс (минимально возможное время),
    // чтобы epoll_wait сработал сразу на следующей итерации
    if (timeout_ms == 0) {
      its.it_value.tv_nsec = 1;
    } else {
      // Переводим миллисекунды в секунды и наносекунды
      its.it_value.tv_sec = timeout_ms / 1000;
      its.it_value.tv_nsec = (timeout_ms % 1000) * 1000000;
    }

    timerfd_settime(curl_timer_fd, 0, &its, nullptr);
  }

  return 0;
}

void init_curl_or_exit() {
  curl_timer_fd = init_timerfd();

  multi_handle = curl_multi_init();
  curl_multi_setopt(multi_handle, CURLMOPT_SOCKETDATA, nullptr);
  curl_multi_setopt(multi_handle, CURLMOPT_SOCKETFUNCTION, cb_socket_action);
  curl_multi_setopt(multi_handle, CURLMOPT_TIMERDATA, nullptr);
  curl_multi_setopt(multi_handle, CURLMOPT_TIMERFUNCTION, cb_timer_action);
}
