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

  std::vector<Parens> sexprs;

  static Ast parse_stream(std::istream&);
};


// ===========================================================================
// Stage 2: abstract compilation into an IR
//
// Turning the above tree representation into a stream of IR instructions.
// This instruction set has no concept of limited registers.
// (Not SSA: we still have labels and plain jumps instead of basic blocks,
//  and variables can be assigned to multiple times)
//
// The instructions closely match the final target instruction set,
// but they operate on abstract values, for example:
//
//    [val5] <- [val1] + [val2]
//
//          instead of
//
//    [r1] <- mem(32)
//    [r3] <- [r0] + [r1]
//
// In this instruction set, loads and stores only happen when requested by
// code. A later codegen pass will color the values onto registers and generate
// appropriate spills.

struct Ir {
  struct Constant { int32_t value; };
  struct Variable { int id; };
  using Value = One_of<Constant, Variable>;

  enum class Op {
    halt,  // no dest, no src1, no src2
    mov,   // no src2
    load,  // no src2
    store, // no dest, src1 is pointer, src2 is value
    add, sub, mul, div, mod,
    cmp_equ, cmp_gt, cmp_lt,
    jump, // no dest, src1 is condition, src2 is target (must be Constant)
  };

  struct Insn {
    Op op;
    Variable dest;
    Value src1;
    Value src2;

    bool has_valid_dest() const;
    bool has_valid_src1() const;
    bool has_valid_src2() const;
  };

  std::vector<Insn> code;
  std::vector<uint32_t> data;
  int num_variables;

  static Ir compile(Ast&);
};


// ===========================================================================
// Stage 3: code generation
//
// This pass knows about how many registers the target processor has, how to
// lay out the code and data in memory, etc.
// It will color values onto registers, spill some into memory, and convert
// abstract instructions into the real ISA.
// Then it will also assemble the result into a binary image.

struct Hw_image {
  std::vector<uint32_t> words;
  uint32_t data_break;

  static Hw_image from_ir(Ir&&);
  void disasm() const;
};