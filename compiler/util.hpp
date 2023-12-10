#pragma once
#include <fmt/core.h>

struct Source_location {
  int line = 1;
  int column = 1;

  void next_column() { column++; }
  void next_line() { column = 1; line++; }
};

template<typename... Args>
[[noreturn]] void error
(Source_location loc, const fmt::format_string<Args...>& fmt, Args&&... args) {
  fmt::print(stderr, "Error at {}:{}: ", loc.line, loc.column);
  fmt::print(stderr, fmt, std::forward<Args>(args)...);
  std::fputc('\n', stderr);
  std::fflush(stderr);
  std::exit(1);
}