#pragma once
#include <cassert>
#include <variant>

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
  decltype(auto) match(Visitors&&... visitors) {
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