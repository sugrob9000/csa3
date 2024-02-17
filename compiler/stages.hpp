#pragma once
#include "util.hpp"
#include <cstdint>
#include <iosfwd>
#include <span>
#include <string>
#include <vector>

// ===========================================================================
// Stage 1: parsing
//
// Turning a text stream into `Ast`, a tree representation of the victim program
// (it's more of an Abstract Syntax Forest, though)

struct Ast {
  struct Node;
  struct Identifier { std::string name; };
  struct Number { int32_t value; };
  struct String { std::string value; };
  struct Parens { std::vector<Node> children; };
  struct Node: One_of<Identifier, Number, String, Parens> {};

  std::vector<Parens> toplevel_exprs;

  static Ast parse_stream(std::istream&);
};

// ===========================================================================
// Stage 2: abstract compilation into an IR
//
// Turning the above tree representation into a stream of "abstract" instructions.
// This instruction set has no concept of limited registers.
// (Not SSA: we still have labels and plain jumps instead of basic blocks,
//  and variables can be assigned to multiple times)
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
// In this instruction set, loads and stores only happen when requested by code.
// A later codegen pass will color the values onto registers and generate
// appropriate spills, too.

struct Constant { int32_t value; };
struct Variable_id { int id; };
using Value = One_of<Constant, Variable_id>;

enum class IR_op {
  halt,  // no dest, no src1, no src2
  mov,   // no src2
  load,  // no src2
  store, // no dest, src1 is addr, src2 is value
  add, sub, mul, div, mod,
  cmp_equ, cmp_gt, cmp_lt,
  jump, // no dest, src1 is condition, src2 is target (must be Constant)
};

struct IR_insn {
  IR_op op;
  Variable_id dest;
  Value src1;
  Value src2;
};

struct IR_output {
  std::vector<IR_insn> code;
  std::vector<uint32_t> data;
  int num_variables;
};
IR_output compile(Ast&);

// ===========================================================================
// Stage 3: code generation
//
// This pass knows about how many registers the target processor has, how to
// lay out the code and data in memory, etc.
// It will color values onto registers, spill some into memory, and convert
// abstract instructions into the real ISA.
// Then it will also assemble the result into a binary image.

void emit_image(std::ostream&, IR_output&&);

void disasm_hw(std::span<const uint32_t> data, std::span<const uint32_t> code);