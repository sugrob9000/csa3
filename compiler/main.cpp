#include "ast.hpp"
#include "ssa.hpp"
#include <iostream>

int main() {
  auto tree = ast::Tree::from_text(std::cin);
  auto ssa = ssa::Sequence::from_lisp_ast(std::move(tree));
}