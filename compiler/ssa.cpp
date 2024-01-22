#include "compiler.hpp"
#include "ssa.hpp"
#include <cassert>
#include <span>

namespace ssa {

Sequence Compiler::compile_lisp_call(ast::Node_call&& call) {
  auto& terms = call.children;
  assert(!terms.empty()); // Should have been an error in parser

  auto& func = terms[0];
  if (!func.is<ast::Node_binding>())
    error(func.location(), "Computation of function names is unsupported");

  auto& func_name = func.as<ast::Node_binding>().name;
  auto arguments = std::span{ terms.begin()+1, terms.end() };

  Sequence result;

  std::vector<Value> inputs;
  inputs.reserve(arguments.size());

  for (auto& argument: arguments) {
    auto input = argument.match(
      [&] (ast::Node_binding&) {
        return Constant{ -123 };
      },
      [&] (ast::Node_constant& constant) {
        return Constant{ constant.value };
      },
      [&] (ast::Node_call& subcall) {
        auto subsequence = compile_lisp_call(std::move(subcall));
        return Constant{ +888 };
      }
    );
    inputs.push_back(input);
  }

  (void) func_name;

  return result;
}

uint32_t Compiler::allocate_variable_id() {
  if (next_variable_id == 0)
    error({}, "Internal compiler error: too many variables in SSA");
  return next_variable_id++;
}

} // namespace ssa