#pragma once
#include <cstdint>
#include <fmt/format.h>

struct Value_src {
  enum class Type: uint8_t { reg = 0, imm = 1 };
  Type type;
  int id_or_value;
};

struct Value_dst {
  int id;
};

// Arithmetic instructions that follow the pattern
//   op dst1, dst2, dest_flag, src1, src2
// (maybe with omissions, for example add has no dst2)
struct Arith_instruction {
  Value_dst dst1, dst2;
  Value_src src1, src2;
  uint8_t dst_flag_id;
};

// Memory instruction
struct Load_instruction { }; // TODO
struct Store_instruction { }; // TODO

struct Invalid_instruction { };

enum class Operation_id: uint8_t {
  add = 0,
  sub = 1,
  mul = 2,
  div = 3,
  load = 10,
  store = 11,
  halt = 31,
};

struct Instruction {
  uint8_t predicate_id;
  bool predicate_state;
  Operation_id op;
  union {
    Arith_instruction arith;
    Load_instruction load;
    Store_instruction store;
    Invalid_instruction invalid;
  };

  // "Canonical" NOP
  constexpr static uint32_t encoded_nop() { return 0x8999'9999; }
  constexpr static Instruction nop() {
    return {
      .predicate_id = 0,
      .predicate_state = true,
      .op = static_cast<Operation_id>(9),
      .invalid = {}
    };
  }
};

Instruction decode(uint32_t);

template<> struct fmt::formatter<Instruction> {
  auto parse(format_parse_context& ctx) { return ctx.begin(); }
  appender format(const Instruction&, format_context& ctx);
};