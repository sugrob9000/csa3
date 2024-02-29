#pragma once
#include <cassert>
#include <optional>
#include <variant>

// A (very) poor man's sum type with matching:
// `std::variant`, augmented with a few convenience methods.

template<typename... Args>
struct Either: std::variant<Args...> {
  using std::variant<Args...>::variant;

  template<typename T>
  bool is() const {
    return std::holds_alternative<T>(*this);
  }

  template<typename T>
  T& as() {
    assert(is<T>());
    return std::get<T>(*this);
  }

  template<typename T>
  T* maybe_as() {
    return is<T>() ? &as<T>() : nullptr;
  }

  template<typename... Fs>
  decltype(auto) match(Fs&&... fs) {
    struct Visitor: Fs... { using Fs::operator()...; };
    return std::visit(Visitor{ std::forward<Fs>(fs)... }, *this);
  }
};


// Polyfill C++23 std::unreachable()

[[noreturn]] inline void unreachable() {
#if NDEBUG
  __builtin_unreachable();
#else
  assert(false);
#endif
}