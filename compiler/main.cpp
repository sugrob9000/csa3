#include "lexparse.hpp"
#include <fmt/format.h>
#include <iostream>

int main() {
  Lexing_stream lexer { std::cin };
  Parser parser;

  while (true) {
    auto token = lexer.next_token();
    if (!token)
      break;

    // Do something simple with the token
    token->visit(
      [] (const tok::Open&) { std::putchar('('); },
      [] (const tok::Close&) { std::putchar(')'); },
      [] (const tok::Quote&) { std::putchar('\''); },
      [] (const tok::Identifier& identifier) { fmt::print("i {}", identifier.name); },
      [] (const tok::Number& number) { fmt::print("n {}", number.value); }
    );
    std::putchar('\n');

    parser.feed(*token);
  }
}