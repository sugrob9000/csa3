#pragma once
#include "diag.hpp"
#include "parse.hpp"
#include <span>

// The "abstract" compiler targets an instruction set that has no concept of
// memory, limited registers, or limited flags. Not quite SSA: we still have
// labels and plain jumps instead of basic blocks.
// Yes, this severely hinders lifetime analysis.
//
// The instructions closely match the final target instruction set,
// but they operate in abstract values, for example:
//
//    [val5] <- [val1] + [val2]
// instead of
//    [r1] <- stackframe[32]
//    [r3] <- [r0] + [r1]
//
// As such, this instruction set has no loads or stores.
// Later, a codegen pass will color the values onto registers and flags
// and generate appropriate spills.

namespace ac {
struct Constant { int32_t value; };
struct Variable_id { uint32_t id; };
using Value = util::Variant<Constant, Variable_id>;

enum class Operation {
  mov, // no operand2
  add, sub, mul, div, mod,
  jump, // no dest, operand1 is condition, operand2 is target (must be Constant)
};

struct Instruction {
  Operation op;
  Variable_id dest;
  Value operand1;
  Value operand2;
};

Value compile_node(Ast::Node&);
Value compile_parens(Ast::Parens&);

std::span<Instruction> get_emitted_code();

} // namespace ac