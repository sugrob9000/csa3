#pragma once
#include <cstdlib>
#include <fmt/core.h>
#include <fmt/format.h>
#include <utility>

namespace util {

template<typename... Args>
void println(const fmt::format_string<Args...>& fmt, Args&&... args) {
  fmt::print(stderr, fmt, std::forward<Args>(args)...);
  std::fputc('\n', stderr);
  std::fflush(stderr);
}

#define LOG(F, ...) (::util::println(FMT_STRING(F) __VA_OPT__(,) __VA_ARGS__))
#define LOG_NOLN(F, ...) (::fmt::print(::stderr, FMT_STRING(F) __VA_OPT__(,) __VA_ARGS__))

#define FATAL(F, ...) \
  do { \
    ::util::println(FMT_STRING(F) __VA_OPT__(,) __VA_ARGS__); \
    ::util::println(FMT_STRING("That was fatal.")); \
    ::std::exit(1); \
  } while (false)

} // namespace util