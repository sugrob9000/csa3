#include "ast.hpp"
#include "ssa.hpp"
#include <fmt/core.h>
#include <iostream>

int main() {
  auto tree = ast::Tree::from_text(std::cin);
  fmt::print(stderr, "{} roots\n", tree.root_expressions.size());

  ssa::Compiler ssa_compiler;

  for (auto& expr: tree.root_expressions) {
    auto ssa = ssa_compiler.compile_lisp_call(std::move(expr));
    fmt::print("ssa inputs: {}, ops: {}\n",
      ssa.inputs.size(),
      ssa.operations.size()
    );
  }
}