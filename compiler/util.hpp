#pragma once
#include <cassert>
#include <fmt/core.h>
#include <variant>

// Represents a source location in the victim Lisp program.
// (Unrelated to `std::source_location`, which refers to the C++ program it's being used in.)
struct Source_location {
  int line = 1;
  int column = 1;
  void next_column() { column++; }
  void next_line() { column = 1; line++; }
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


namespace util {

// `std::variant`, augmented with a few convenience methods.
template<typename... Args>
class Variant: std::variant<Args...> {
  using Base = std::variant<Args...>;

public:
  using Base::Base;

  template<typename T> bool is() const {
    return std::holds_alternative<T>(*this);
  }

  template<typename T> T& as() {
    assert(is<T>());
    return std::get<T>(*this);
  }

  template<typename... Visitors>
  decltype(auto) visit(Visitors&&... visitors) {
    struct Overloaded_visitor: Visitors... {
      using Visitors::operator()...;
    };
    return std::visit(
      Overloaded_visitor{ std::forward<Visitors>(visitors)... },
      static_cast<Base&>(*this)
    );
  }
};


// Polyfill for `std::unreachable` until C++23
[[noreturn]] inline void unreachable() { __builtin_unreachable(); }

} // namespace util