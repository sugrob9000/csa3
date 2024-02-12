#include "diag.hpp"
#include "parse.hpp"
#include "util.hpp"
#include <charconv>
#include <cstdint>
#include <iostream>
#include <optional>
#include <vector>

namespace {

struct Open_token {};
struct Close_token {};
struct Identifier_token { std::string name; };
struct Number_token { int32_t value; };
struct String_token { std::string value; };

using Token = util::Variant<
  Open_token,
  Close_token,
  Identifier_token,
  Number_token,
  String_token
>;

bool is_identifier_char(char c) {
  if (c == '(' || c == ')' || c == ';' || c == '"')
    return false;
  // Other than the special characters, Lisps are a lot laxer about what
  // can be in an identifier. '-', '+', and many others are allowed.
  return std::isprint(c) && !std::isspace(c);
}

class Lexer {
  std::istream& is;

  std::optional<char> peek() {
    int c = is.peek();
    if (c == -1)
      return std::nullopt;
    return char(c);
  }

  // Must not be called without making sure there is something in the stream with peek().
  // This is expressed with the requirement to provide an expected character.
  void consume_expect(char expected) {
    int c = is.get();
    (void) expected;
    (void) c;
    assert(c != -1);
    assert(c == expected);
  }

  std::optional<char> peek_after_whitespace() {
    bool inside_comment = false;
    while (true) {
      auto c = peek();
      if (!c)
        return std::nullopt;

      if (*c == ';')
        inside_comment = true;
      else if (*c == '\n')
        inside_comment = false;

      if (!std::isspace(*c) && !inside_comment)
        return c;

      consume_expect(*c);
    }
  }

  // A multichar token is either an identifier or a number
  Token consume_multichar() {
    std::string word;
    while (true) {
      auto c = peek();
      if (!c || !is_identifier_char(*c))
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
        error("Bad integer literal '{}'. charconv says '{}'",
            word, std::make_error_code(ec).message());
      }

      return Number_token(number);
    } else {
      // This is just a identifier (function or binding name, etc.)
      return Identifier_token(std::move(word));
    }
  }

  Token consume_string_literal() {
    assert(*peek() == '"');
    consume_expect('"');

    std::string literal;
    while (true) {
      auto c = peek();
      if (!c)
        error("EOF before closing string literal");
      consume_expect(*c);
      if (*c == '"')
        break;
      literal.push_back(*c);
    }

    return String_token(std::move(literal));
  }

public:
  explicit Lexer(std::istream& is_): is{is_} {}

  std::optional<Token> consume_token() {
    auto peeked = peek_after_whitespace();
    if (!peeked)
      return std::nullopt;
    switch (*peeked) {
    case '(':
      consume_expect(*peeked);
      return Open_token{};
    case ')':
      consume_expect(*peeked);
      return Close_token{};
    case '"':
      return consume_string_literal();
    default:
      return consume_multichar();
    }
  }
};

} // anon namespace

Ast Ast::parse_stream(std::istream& is) {
  Ast tree;
  Lexer lexer(is);

  // `Parens::children` can cause relocation of children, but we only modify the
  // node at the stack's top, so invalidation can't happen where we can't expect it
  std::vector<Parens*> stack;

  while (true) {
    auto token = lexer.consume_token();
    if (!token)
      break;

    if (stack.empty()) {
      // Root context is special: only parens can appear here
      token->match(
        [&] (Open_token) { stack.push_back(&tree.toplevel_exprs.emplace_back()); },
        [&] (auto&&) { error("At root scope, only opening parens is allowed"); }
      );
      continue;
    }

    // General case
    auto& context = stack.back()->children;
    token->match(
      [&] (Open_token) {
        context.emplace_back(Parens{});
        auto new_top = &context.back().as<Parens>();
        stack.push_back(new_top);
      },
      [&] (Close_token) {
        if (context.empty())
          error("Empty parens make no sense");
        stack.pop_back();
      },
      [&] (Identifier_token& id) {
        context.emplace_back(Identifier{std::move(id.name)});
      },
      [&] (Number_token& num) {
        context.emplace_back(Number{num.value});
      },
      [&] (String_token& str) {
        context.emplace_back(String{std::move(str.value)});
      }
    );
  }

  return tree;
}