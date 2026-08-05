#include "embed.hpp"
#include <string_view>

constexpr unsigned char dialog_raw[] = {
    0x12, // ID 18
#embed "../build/dialog"
};

constexpr std::string_view not_ok_headers = "HTTP/1.1 400 Not OK\r\n"
                                            "Content-Type: text/html; charset=utf-8\r\n"
                                            "Connection: close\r\n"
                                            "\r\n";
constexpr unsigned char not_ok_html_raw[] = {
#embed "html/not_ok.html"
};

constexpr std::string_view ok_headers = "HTTP/1.1 200 OK\r\n"
                                        "Content-Type: text/html; charset=utf-8\r\n"
                                        "Connection: close\r\n"
                                        "\r\n";

constexpr unsigned char ok_html_raw[] = {
#embed "html/ok.html"
};

template <size_t N> consteval auto buildFullPacket(const unsigned char (&body)[N]) {
  // 5 байт макс. под VarInt + размер body
  std::array<unsigned char, 5 + N> result{};
  size_t offset = 0;

  // --- Встроенная логика VarInt ---
  int value = static_cast<int>(N);
  while (true) {
    unsigned char temp = static_cast<unsigned char>(value & 0x7F);
    value >>= 7;

    if (value != 0)
      temp |= 0x80;

    result[offset++] = temp;

    if (value == 0)
      break;
  }

  // --- Копирование body следом ---
  for (size_t i = 0; i < N; ++i) {
    result[offset++] = body[i];
  }

  return std::pair{result, offset};
}

constexpr auto packed = buildFullPacket(dialog_raw);

template <size_t NHeader, size_t NBody> consteval auto make_http_response(std::string_view head, const unsigned char (&body)[NBody]) {
  std::array<unsigned char, NHeader + NBody> result{};
  for (size_t i = 0; i < NHeader; ++i)
    result[i] = static_cast<char>(head[i]);
  for (size_t i = 0; i < NBody; ++i)
    result[NHeader + i] = body[i];
  return result;
}

constexpr auto not_ok_html_array = make_http_response<not_ok_headers.size(), sizeof(not_ok_html_raw)>(not_ok_headers, not_ok_html_raw);
constexpr auto ok_html_array = make_http_response<ok_headers.size(), sizeof(ok_html_raw)>(ok_headers, ok_html_raw);

namespace embedded {
const std::span<const unsigned char> dialog{packed.first.data(), packed.second};
const std::span<const unsigned char> not_ok_html{not_ok_html_array};
const std::span<const unsigned char> ok_html{ok_html_array};
} // namespace embedded
