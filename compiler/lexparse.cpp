#include "lexparse.hpp"
#include <cassert>
#include <cctype>
#include <charconv>
#include <iostream>

Token Token::from_single_char(char c) {
  switch (c) {
  case '(': return tokens::Open{};
  case ')': return tokens::Close{};
  case '\'': return tokens::Quote{};
  default: assert(!"Other single characters cannot form a complete token");
  }
}

Token Token::word(std::string name) { return tokens::Word(std::move(name)); }
Token Token::literal_u32(uint32_t value) { return tokens::Literal_u32(value); }
Token Token::literal_str(std::string value) { return tokens::Literal_str(std::move(value)); }


static bool is_word_cont_char(char c) {
  switch (c) {
  case '"':
  case '\'':
  case '(':
  case ')':
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

void Lexing_stream::consume_char() {
  int c = is.get();
  assert(c != EOF);
  if (c == '\n')
    loc.next_line();
  else
    loc.next_column();
}

std::optional<char> Lexing_stream::peek_after_whitespace() {
  while (true) {
    auto c = peek();
    if (!c)
      return std::nullopt;
    if (!std::isspace(*c))
      return *c;
    consume_char();
  }
}

Token Lexing_stream::consume_word() {
  std::string word;

  while (true) {
    auto c = peek();
    if (!c || !is_word_cont_char(*c))
      break;
    consume_char();
    word.push_back(*c);
  }

  if (std::isdigit(word[0])) {
    // This is an attempt at a number
    uint32_t number;
    auto [ptr, ec] = std::from_chars(word.data(), word.data() + word.size(), number);
    if (ec != std::errc{})
      error(loc, "Bad integer literal '{}': {}", word, std::make_error_code(ec).message());
    return Token::literal_u32(number);
  } else {
    // This is just a word (function or binding name, etc.)
    return Token::word(std::move(word));
  }
}

Token Lexing_stream::consume_string() {
  assert(*peek() == '"');
  consume_char();

  std::string literal;

  while (true) {
    auto maybe_c = peek();
    if (!maybe_c)
      error(loc, "Source file ended without closing string literal");
    consume_char();
    if (*maybe_c == '"')
      break;
    literal.push_back(*maybe_c);
  }

  return Token::literal_str(std::move(literal));
}

std::optional<Token> Lexing_stream::next_token() {
  auto next_peeked = peek_after_whitespace();
  if (!next_peeked)
    return std::nullopt;
  switch (*next_peeked) {
  case '(':
  case ')':
  case '\'':
    consume_char();
    return Token::from_single_char(*next_peeked);
  case '"': return consume_string();
  default: return consume_word();
  }
}