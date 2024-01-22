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
  std::vector<Variable> inputs;
  std::vector<Operation> operations;
};

class Compiler {
  uint32_t next_variable_id = 1;
  uint32_t allocate_variable_id();

  struct Arg_requirement {
    enum class Type { at_least, exactly } type;
    unsigned how_many;
  };

  struct Function {
    Arg_requirement arg_requirement;
  };

public:
  Sequence compile_lisp_call(ast::Node_call&&);
};

} // namespace ssa