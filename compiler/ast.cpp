#include "ast.hpp"
#include "compiler.hpp"
#include "util.hpp"
#include <charconv>
#include <cstdint>
#include <iostream>
#include <optional>
#include <vector>

namespace {

struct Open_token {};
struct Close_token {};
struct Quote_token {};

struct Identifier_token { std::string name; };
struct Number_token { int32_t value; };

using Any_token = util::Variant<
  Open_token,
  Close_token,
  Quote_token,
  Identifier_token,
  Number_token
>;

class Token: Any_token {
  using Any_token::Any_token;

public:
  using Any_token::match, Any_token::is, Any_token::as;

  static Token from_single_char(char c) {
    switch (c) {
    case '(': return Open_token{};
    case ')': return Close_token{};
    case '\'': return Quote_token{};
    default:
      assert(!"Other single characters cannot form a complete token");
      util::unreachable();
    }
  }

  static Token identifier(std::string ident) {
    return Identifier_token(std::move(ident));
  }

  static Token number(int32_t n) {
    return Number_token(n);
  }
};

class Lexer {
  std::istream& is;
  Source_location loc;

  using Maybe_char = std::optional<char>;

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

  Maybe_char peek() {
    int c = is.peek();
    if (c == -1)
      return std::nullopt;
    return char(c);
  }

  void consume_expect(char expected) {
    int c = is.get();
    (void) expected;
    assert(c != -1);
    assert(c == expected);
    if (c == '\n')
      loc = loc.next_line();
    else
      loc = loc.next_column();
  }

  Maybe_char peek_after_whitespace() {
    bool inside_comment = false;
    while (true) {
      Maybe_char c = peek();
      if (!c)
        return std::nullopt;

      if (*c == ';')
        inside_comment = true;
      if (*c == '\n')
        inside_comment = false;

      if (!std::isspace(*c) && !inside_comment)
        return c;

      consume_expect(*c);
    }
  }

  // A multichar token is either an identifier or a number.
  // There are no string literals.
  Token consume_multichar() {
    std::string word;
    while (true) {
      Maybe_char c = peek();
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
        error(loc, "Bad integer literal '{}'. charconv says '{}'",
            word, std::make_error_code(ec).message());
      }
      return Token::number(number);
    } else {
      // This is just a identifier (function or binding name, etc.)
      return Token::identifier(std::move(word));
    }
  }

public:
  explicit Lexer(std::istream& is_): is{is_} {}

  std::optional<Token> consume_token() {
    Maybe_char peeked = peek_after_whitespace();
    if (!peeked)
      return std::nullopt;
    switch (*peeked) {
    case '(':
    case ')':
    case '\'':
      consume_expect(*peeked);
      return Token::from_single_char(*peeked);
    default:
      return consume_multichar();
    }
  }
};

} // anon namespace

auto ast::Tree::from_text(std::istream& is) -> Tree {
  Tree tree;
  Lexer lexer(is);

  // `Node_call::children` can cause relocation of children, but we only modify the
  // node at the stack's top, so invalidation can't happen where we can't expect it
  std::vector<Node_call*> stack;

  while (true) {
    auto token = lexer.consume_token();
    if (!token)
      break;

    if (stack.empty()) {
      // Root context is special: only calls are allowed
      token->match(
        [&] (Open_token) { stack.push_back(&tree.root_expressions.emplace_back()); },
        [] (auto&&) { error({}, "Only paren-expression allowed at file scope"); }
      );
      continue;
    }

    // General case
    auto& top = stack.back()->children;
    token->match(
      [&] (Open_token) {
        top.push_back(Node_call{});
        auto new_top = &top.back().as<Node_call>();
        stack.push_back(new_top);
      },
      [&] (Close_token) { stack.pop_back(); },
      [&] (Identifier_token& id) { top.push_back(Node_binding{std::move(id.name)}); },
      [&] (Number_token& num) { top.push_back(Node_constant{num.value}); },
      [&] (Quote_token) { error({}, "Quotes are not supported"); }
    );
  }

  return tree;
}