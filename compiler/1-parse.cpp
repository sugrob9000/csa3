#include "diagnostics.hpp"
#include "stages.hpp"
#include "util.hpp"
#include <charconv>
#include <cstdint>
#include <iostream>
#include <optional>
#include <vector>

namespace {

struct Lexer {
  std::istream& is;

  struct Opening_paren {};
  struct Closing_paren {};
  struct Identifier { std::string name; };
  struct Number { int32_t value; };
  struct String { std::string value; };

  using Token = Either<
    Opening_paren,
    Closing_paren,
    Identifier,
    Number,
    String
  >;

  static bool is_identifier_char(char c) {
    if (c == '(' || c == ')' || c == ';' || c == '"')
      return false;
    // Other than the special characters, Lisps are a lot laxer about what
    // can be in an identifier. '-', '+', and many others are allowed.
    return std::isprint(c) && !std::isspace(c);
  }

  std::optional<char> peek() {
    int c = is.peek();
    if (c == -1)
      return std::nullopt;
    return char(c);
  }

  // Must not be called without making sure there is something in the stream with peek().
  // This is expressed with the requirement to provide an expected character.
  void consume_expect([[maybe_unused]] char expected) {
    [[maybe_unused]] int c = is.get();
    assert(c != -1);
    assert(c == expected);
  }

  std::optional<char> peek_after_whitespace() {
    bool inside_comment = false;
    while (auto c = peek()) {
      if (*c == ';')
        inside_comment = true;
      else if (*c == '\n')
        inside_comment = false;
      if (!std::isspace(*c) && !inside_comment)
        return c;
      consume_expect(*c);
    }
    return std::nullopt;
  }

  // A multichar token is either an identifier or a number
  Token consume_multichar() {
    std::string word;
    while (auto c = peek()) {
      if (!is_identifier_char(*c))
        break;
      consume_expect(*c);
      word.push_back(*c);
    }

    assert(!word.empty());

    // '+', '-' can start a number, but should be identifiers on their own
    const bool is_number = std::isdigit(word[0])
      || ((word.length() >= 2) &&
          (word[0] == '-' || word[0] == '+'));

    if (is_number) {
      // This is an attempt at a number
      int32_t number;
      auto [ptr, ec] = std::from_chars(word.data(), word.data() + word.size(), number);
      if (ec != std::errc{}) {
        error(
          "Bad integer literal '{}'. charconv says '{}'",
          word,
          std::make_error_code(ec).message()
        );
      }
      return Number(number);
    } else {
      // This is just an identifier
      return Identifier(std::move(word));
    }
  }

  Token consume_string_literal() {
    consume_expect('"');
    for (std::string literal; auto c = peek(); ) {
      consume_expect(*c);
      if (*c == '"')
        return String(std::move(literal));
      literal.push_back(*c);
    }
    error("EOF before closing string literal");
  }

  std::optional<Token> consume_token() {
    auto peeked = peek_after_whitespace();
    if (!peeked)
      return std::nullopt;
    switch (*peeked) {
    case '(':
      consume_expect(*peeked);
      return Opening_paren{};
    case ')':
      consume_expect(*peeked);
      return Closing_paren{};
    case '"': return consume_string_literal();
    default: return consume_multichar();
    }
  }
};

} // anon namespace

Ast Ast::parse_stream(std::istream& is) {
  // `Parens::children` can cause relocation of children, but we only modify the
  // node at the stack's top, so invalidation can't happen where we can't expect it
  std::vector<Parens*> stack;
  Ast tree;

  for (Lexer lexer(is); auto token = lexer.consume_token(); ) {
    if (stack.empty()) {
      // Root context is special: only parens can appear here
      token->match(
        [&] (Lexer::Opening_paren) { stack.push_back(&tree.sexprs.emplace_back()); },
        [&] (Lexer::Closing_paren) { error("Unbalanced parens: too many closing"); },
        [&] (auto&&) { error("At root scope, only opening parens is allowed"); }
      );
      continue;
    }

    // General case
    auto& context = stack.back()->children;
    token->match(
      [&] (Lexer::Opening_paren) {
        auto new_top = &context.emplace_back(Parens{}).as<Parens>();
        stack.push_back(new_top);
      },
      [&] (Lexer::Closing_paren) {
        if (context.empty())
          error("Empty parens make no sense");
        stack.pop_back();
      },
      [&] (Lexer::Identifier& id) { context.emplace_back(Identifier{std::move(id.name)}); },
      [&] (Lexer::Number& num) { context.emplace_back(Number{num.value}); },
      [&] (Lexer::String& str) { context.emplace_back(String{std::move(str.value)}); }
    );
  }

  if (!stack.empty())
    error("Unbalanced parens: too many opening");

  return tree;
}