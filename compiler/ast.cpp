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
struct Identifier_token { std::string name; };
struct Number_token { int32_t value; };

using Any_token = util::Variant<
  Open_token,
  Close_token,
  Identifier_token,
  Number_token
>;

class Token: Any_token {
  Source_location location_;
public:
  using Any_token::match, Any_token::is, Any_token::as;

  Token(Any_token&& base, Source_location loc):
    Any_token(std::move(base)), location_(loc) {}

  Source_location location() const { return location_; }
};

bool is_identifier_char(char c) {
  if (c == '(' || c == ')' || c == ';')
    return false;
  // Other than the special characters, Lisps are a lot laxer about what
  // can be in an identifier. '-', '+', and many others are allowed.
  return std::isprint(c) && !std::isspace(c);
}

class Lexer {
  std::istream& is;
  Source_location location;

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
    assert(c != -1);
    assert(c == expected);
    if (c == '\n')
      location = location.next_line();
    else
      location = location.next_column();
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

  // A multichar token is either an identifier or a number. There are no string literals.
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
        error(location, "Bad integer literal '{}'. charconv says '{}'",
            word, std::make_error_code(ec).message());
      }

      return { Number_token(number), location };
    } else {
      // This is just a identifier (function or binding name, etc.)
      return { Identifier_token(std::move(word)), location };
    }
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
      return Token(Open_token{}, location);
    case ')':
      consume_expect(*peeked);
      return Token(Close_token{}, location);
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
        [&] (Open_token) {
          stack.push_back(&tree.root_expressions.emplace_back());
        },
        [&] (auto&&) {
          error(token->location(), "At file scope, only opening parens is allowed");
        }
      );
      continue;
    }

    // General case
    auto& context = stack.back()->children;
    token->match(
      [&] (Open_token) {
        context.emplace_back(Node_call{}, token->location());
        auto new_top = &context.back().as<Node_call>();
        stack.push_back(new_top);
      },
      [&] (Close_token) {
        if (context.empty())
          error(token->location(), "Empty parens make no sense");
        stack.pop_back();
      },
      [&] (Identifier_token& id) {
        context.emplace_back(Node_binding{std::move(id.name)}, token->location());
      },
      [&] (Number_token& num) {
        context.emplace_back(Node_constant{num.value}, token->location());
      }
    );
  }

  return tree;
}