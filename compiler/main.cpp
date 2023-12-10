#include "lexparse.hpp"
#include <fmt/format.h>
#include <iostream>

int main() {
  Lexing_stream lexer { std::cin };

  while (true) {
    auto maybe_token = lexer.next_token();
    if (!maybe_token)
      break;
    Token& token = *maybe_token;

    // Do something simple with the token
    token.visit(
        [] (const tokens::Open&) { std::putchar('('); },
        [] (const tokens::Close&) { std::putchar(')'); },
        [] (const tokens::Quote&) { std::putchar('\''); },
        [] (const tokens::Word& word) { fmt::print("w {}", word.name); },
        [] (const tokens::Literal_u32& u32) { fmt::print("i {}", u32.value); },
        [] (const tokens::Literal_str& str) { fmt::print("s {}", str.value); }
    );

    std::putchar('\n');
  }
}