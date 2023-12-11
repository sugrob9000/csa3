// S-expression lexer and parser.
// `Lexing_stream` consumes an istream and ouptuts `Token`s.
// `Parser` consumes `Token`s and builds an S-exp tree, plus some auxillary data

#pragma once
#include "util.hpp"
#include <cstdint>
#include <iosfwd>
#include <optional>
#include <string>
#include <vector>

namespace tok {

struct Open {};
struct Close {};
struct Quote {};

struct Identifier { std::string name; };
struct Number { uint32_t value; };

using Any = util::Variant<Open, Close, Quote, Identifier, Number>;

} // namespace tok

class Token: tok::Any {
  using tok::Any::Any;
public:
  using tok::Any::visit, tok::Any::is, tok::Any::as;
  static Token from_single_char(char);
  static Token identifier(std::string);
  static Token number(uint32_t);
};


class Lexing_stream {
  std::istream& is;
  Source_location current_location;

  std::optional<char> peek();
  std::optional<char> peek_after_whitespace();
  void consume_expect(char);
  Token consume_identifier();
  Token consume_string();

public:
  explicit Lexing_stream(std::istream& is_): is(is_) {}
  std::optional<Token> next_token();
};


namespace ast {

struct Node;

struct Node_identifier { std::string name; };
struct Node_number { uint32_t value; };
struct Node_call { std::vector<Node> children; };
using Node_any = util::Variant<Node_identifier, Node_number, Node_call>;

struct Node: private Node_any {
  using Node_any::Node_any, Node_any::as;
};

struct Tree {
  std::vector<Node_call> root_expressions;
};

} // namespace ast

struct Parser {
  ast::Tree tree;

  // `Node_call::children` can cause relocation of children, but we can only modify the
  // node at the stack's top, so invalidation can't happen where we can't expect it
  std::vector<ast::Node_call*> stack;

  void feed(Token);
};