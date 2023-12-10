// S-expression lexer and parser.
// `Lexing_stream` consumes an istream and ouptuts `Token`s.
// `Parser` consumes `Token`s and builds an S-exp tree, plus some auxillary data

#pragma once
#include "util.hpp"
#include <cassert>
#include <cstdint>
#include <iosfwd>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace tokens {

struct Open {};
struct Close {};
struct Quote {};

struct Word { std::string name; };
struct Literal_u32 { uint32_t value; };
struct Literal_str { std::string value; };

using Any = std::variant<
  tokens::Open,
  tokens::Close,
  tokens::Quote,
  tokens::Word,
  tokens::Literal_u32,
  tokens::Literal_str
>;

} // namespace tokens

class Token: tokens::Any {
  using tokens::Any::Any;
public:
  template<typename T> bool is() const {
    return std::holds_alternative<T>(*this);
  }

  template<typename T> T& as() {
    assert(is<T>());
    return std::get<T>(*this);
  }

  static Token from_single_char(char);
  static Token word(std::string);
  static Token literal_u32(uint32_t);
  static Token literal_str(std::string);

  template<typename... Visitors>
  decltype(auto) visit(Visitors&&... visitors) {
    struct Overloaded_visitor: Visitors... { using Visitors::operator()...; };
    return std::visit(
      Overloaded_visitor{ std::forward<Visitors>(visitors)... },
      static_cast<tokens::Any&>(*this)
    );
  }
};


class Lexing_stream {
  std::istream& is;
  Source_location loc;

  std::optional<char> peek();
  std::optional<char> peek_after_whitespace();
  void consume_char();
  Token consume_word();
  Token consume_string();

public:
  explicit Lexing_stream(std::istream& is_): is(is_) {}

  std::optional<Token> next_token();
};


struct Abstract_syntax_tree {
  struct Node {
    std::vector<Node> children;
  };
  Node root;
};

struct Parser {
  Abstract_syntax_tree tree;

  std::vector<Abstract_syntax_tree::Node*> stack;

  void feed(Token);
};