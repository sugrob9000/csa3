#pragma once
#include "ast.hpp"
#include "util.hpp"
#include <cstdint>
#include <vector>

namespace ssa {

struct Variable { uint32_t id; };
struct Constant { int32_t value; };
using Value = util::Variant<Variable, Constant>;

struct Operation {
  Variable output;
  std::vector<Value> inputs;

  enum class Type {
    // inputs.size() == 2
    add, sub, mul, div,
    // inputs[0] = function ID
    call,
  };
  Type type;
};

struct Sequence {
  Variable output;
  std::vector<Value> inputs;
  std::vector<Operation> operations;
  static Sequence from_lisp_ast(ast::Tree&&);
};

} // namespace ssa