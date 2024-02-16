#include "stages.hpp"
#include <iostream>

int main() {
  auto ast = Ast::parse_stream(std::cin);
  auto compiler_output = compile(ast);
  emit_image(std::cout, std::move(compiler_output));
}
