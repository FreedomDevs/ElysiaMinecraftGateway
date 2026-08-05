#pragma once
#include <curl/curl.h>

inline CURLM *multi_handle;
inline int curl_timer_fd;

int cb_socket_action(CURL *easy, curl_socket_t s, int action, void *userp, void *socketp);
int cb_timer_action(CURLM *multi, long timeout_ms, void *userp);

void init_curl_or_exit();
