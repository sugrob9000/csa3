// Compiler-wide utilities

#pragma once
#include <fmt/core.h>

// Represents a source location in the victim Lisp program.
// (Unrelated to `std::source_location`, which refers to the C++ program it's being used in.)
struct Source_location {
  int line = 1;
  int column = 1;
  [[nodiscard]] Source_location next_line() const { return { line + 1, 1 }; }
  [[nodiscard]] Source_location next_column() const { return { line, column + 1 }; }
};

// Compiler error, for when the victim Lisp program is beyond repair.
template<typename... Args>
[[noreturn]] void error
(Source_location loc, const fmt::format_string<Args...>& fmt, Args&&... args) {
  fmt::print(stderr, "Error at {}:{}: ", loc.line, loc.column);
  fmt::print(stderr, fmt, std::forward<Args>(args)...);
  std::fputc('\n', stderr);
  std::fflush(stderr);
  std::exit(1);
}