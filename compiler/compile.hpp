#pragma once
#include "diag.hpp"
#include "parse.hpp"
#include <vector>

// The "abstract" compiler targets an instruction set that has no concept of
// memory or limited registers, etc. Not quite SSA: we still have labels and
// plain jumps instead of basic blocks. Yes, this severely hinders lifetime analysis.
//
// The instructions closely match the final target instruction set,
// but they operate in abstract values, for example:
//
//    [val5] <- [val1] + [val2]
//
//          instead of
//
//    [r1] <- mem(32)
//    [r3] <- [r0] + [r1]
//
// As such, this instruction set has no loads or stores.
// A later codegen pass will color the values onto registers
// and generate appropriate spills.

namespace cc {

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

struct Compiler_output {
  std::vector<Instruction> code;
  std::vector<uint32_t> data;
};
Compiler_output compile(Ast&);

} // namespace cc