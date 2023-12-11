#include "lexparse.hpp"
#include <cassert>
#include <cctype>
#include <charconv>
#include <iostream>

Token Token::from_single_char(char c) {
  switch (c) {
  case '(': return tok::Open{};
  case ')': return tok::Close{};
  case '\'': return tok::Quote{};
  default:
    assert(!"Other single characters cannot form a complete token");
    util::unreachable();
  }
}

Token Token::identifier(std::string name) { return tok::Identifier(std::move(name)); }
Token Token::number(uint32_t value) { return tok::Number(value); }


static bool is_identifier_char(char c) {
  // Lisps are a lot laxer about what can be in an identifier.
  // '-', '+', and many others are allowed.
  switch (c) {
  case '\'':
  case '(':
  case ')':
  case ';':
    return false;
  default:
    return std::isprint(c) && !std::isspace(c);
  }
}

std::optional<char> Lexing_stream::peek() {
  int c = is.peek();
  if (c == EOF)
    return std::nullopt;
  return char(c);
}

void Lexing_stream::consume_expect(char expected) {
  int c = is.get();
  (void) expected;
  assert(c != EOF);
  assert(c == expected);

  if (c == '\n')
    current_location.next_line();
  else
    current_location.next_column();
}

std::optional<char> Lexing_stream::peek_after_whitespace() {
  bool in_comment = false;

  while (true) {
    auto c = peek();
    if (!c)
      return std::nullopt;

    if (*c == ';')
      in_comment = true;
    if (*c == '\n')
      in_comment = false;

    if (!std::isspace(*c) && !in_comment)
      return c;

    consume_expect(*c);
  }
}

Token Lexing_stream::consume_identifier() {
  std::string identifier;

  while (true) {
    auto c = peek();
    if (!c || !is_identifier_char(*c))
      break;
    consume_expect(*c);
    identifier.push_back(*c);
  }

  // We should not have been called without knowing
  // there's at least a 1-long identifier in the stream
  assert(!identifier.empty());

  if (std::isdigit(identifier[0])) {
    // This is an attempt at a number
    uint32_t number;
    auto [ptr, ec]
      = std::from_chars(identifier.data(), identifier.data() + identifier.size(), number);
    if (ec != std::errc{}) {
      error(current_location, "Bad integer literal '{}': {}",
          identifier, std::make_error_code(ec).message());
    }
    return Token::number(number);
  } else {
    // This is just a identifier (function or binding name, etc.)
    return Token::identifier(std::move(identifier));
  }
}

std::optional<Token> Lexing_stream::next_token() {
  auto peeked = peek_after_whitespace();
  if (!peeked)
    return std::nullopt;
  switch (*peeked) {
  case '(':
  case ')':
  case '\'':
    consume_expect(*peeked);
    return Token::from_single_char(*peeked);
  default: return consume_identifier();
  }
}


void Parser::feed(Token token) {
  using namespace ast;

  if (stack.empty()) {
    // Root context is slightly special: only calls are allowed here,
    // so error out on anything that is not an open paren
    token.visit(
      [&] (tok::Open) { stack.push_back(&tree.root_expressions.emplace_back()); },
      [] (auto&&) { error({}, "Only paren-expressions allowed at root scope"); }
    );
    return;
  }

  auto& children = stack.back()->children;

  token.visit(
    [&] (tok::Open) {
      children.push_back(Node_call{});
      stack.push_back(&children.back().as<Node_call>());
    },
    [&] (tok::Close) { stack.pop_back(); },
    [] (tok::Quote) { error({}, "Quotes are unsupported yet"); },
    [&] (const tok::Identifier& identifier) {
      children.push_back(Node_identifier(identifier.name));
    },
    [&] (const tok::Number& number) {
      children.push_back(Node_number(number.value));
    }
  );
}