#include "insn.hpp"
#include "util.hpp"
#include <cassert>

static uint32_t extract(uint32_t value, uint8_t from, uint8_t upto) {
  return (value >> from) & ((1 << (upto - from)) - 1);
}

static Value_src decode_src(uint8_t encoded) {
  assert(encoded == extract(encoded, 0, 5)); // no junk bits

  const uint8_t src_type = extract(encoded, 0, 1);
  const uint8_t value = extract(encoded, 1, 5);

  return Value_src {
    .type = static_cast<Value_src::Type>(src_type),
    .id_or_value = value
  };
}

static Value_dst decode_dst(uint8_t encoded) {
  assert(encoded == extract(encoded, 0, 4)); // no junk bits
  return Value_dst { .id = encoded };
}


Instruction decode(uint32_t encoded) {
  Instruction result;

  result.predicate_id = extract(encoded, 0, 3);
  result.predicate_state = extract(encoded, 3, 4);

  result.op = static_cast<Operation_id>(extract(encoded, 4, 9));
  switch (result.op) {
  case Operation_id::sub:
  case Operation_id::add: {
    result.arith.src1 = decode_src(extract(encoded, 9, 14));
    result.arith.src2 = decode_src(extract(encoded, 14, 19));
    result.arith.dst1 = decode_dst(extract(encoded, 19, 23));
    result.arith.dst2 = {};
    result.arith.dst_flag_id = extract(encoded, 23, 26);
    break;
  }
  default:
    break;
  }

  return result;
}


auto fmt::formatter<Instruction>::format
(const Instruction& insn, format_context& ctx) -> appender {
  *ctx.out()++ = '[';

  if (insn.predicate_id == 0)
    format_to(ctx.out(), "{} ", insn.predicate_state ? "nx" : "  ");
  else
    format_to(ctx.out(), "{}{} ", insn.predicate_state ? '+' : '-', insn.predicate_id);

  const string_view op_mnemonic = [&] {
    switch (insn.op) {
    case Operation_id::add: return "add";
    case Operation_id::sub: return "sub";
    case Operation_id::mul: return "mul";
    case Operation_id::div: return "div";
    case Operation_id::load: return "load";
    case Operation_id::store: return "store";
    case Operation_id::halt: return "halt";
    }
    return "ud";
  } ();
  format_to(ctx.out(), "{} ", op_mnemonic);

  switch (insn.op) {
    // TODO
  case Operation_id::add:
  case Operation_id::sub:
  case Operation_id::mul:
  case Operation_id::div: {
    auto& ar = insn.arith;
    const auto src_prefix = [] (const Value_src& src) {
      return src.type == Value_src::Type::reg ? 'r' : 'i';
    };
    format_to(ctx.out(), "{}{} {}{} -> r{} r{} f{}",
        src_prefix(ar.src1), ar.src1.id_or_value,
        src_prefix(ar.src2), ar.src2.id_or_value,
        ar.dst1.id, ar.dst2.id,
        ar.dst_flag_id);
    break;
  }
  case Operation_id::load:
  case Operation_id::store:
  case Operation_id::halt:
    break;
  default:
    format_to(ctx.out(), "0x{:02x}", static_cast<uint8_t>(insn.op));
    break;
  }

  *ctx.out()++ = ']';
  return ctx.out();
}