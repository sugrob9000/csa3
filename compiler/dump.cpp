#include "stages.hpp"
#include <fmt/core.h>
#include <span>

struct Binop_src { uint32_t encoded; };

template<>
struct fmt::formatter<Binop_src> {
  constexpr auto parse(format_parse_context& ctx) { return ctx.begin(); }
  appender format(Binop_src src, format_context& ctx) {
    if (src.encoded & 1)
      return format_to(ctx.out(), "r{}", src.encoded >> 1);
    else
      return format_to(ctx.out(), "0x{:x}", src.encoded >> 1);
  }
};

void disasm_hw(std::span<const uint32_t> data, std::span<const uint32_t> code) {
  int addr = 0;
  for (uint32_t d: data)
    fmt::print("{:3x}: (data) 0x{:x}\n", addr++, d);
  constexpr std::string_view insn_names[] = {
    "halt",
    "ld", "st",
    "add", "sub", "mul", "div", "mod",
    "equ", "gt ", "lt ",
    "jmp", "jif",
  };
  for (uint32_t insn: code) {
    uint32_t opcode = insn & 0xF;
    fmt::print("{:3x}: {} ", addr++, insn_names[opcode], insn >> 4);
    switch (opcode) {
    case 0x0:
      break;
    case 0x1:
    case 0x2:
      fmt::print("r{}, 0x{:x}", (insn >> 4) & 0x3F, insn >> 11);
      break;
    case 0xB:
      fmt::print("0x{:x}", insn >> 4);
      break;
    case 0xC:
      fmt::print("r{}, 0x{:x}", (insn >> 4) & 0x3F, insn >> 10);
      break;
    default:
      fmt::print("r{}, {}, {}", (insn >> 4) & 0x3F,
          Binop_src((insn >> 10) & 0x7FF),
          Binop_src(insn >> 21));
      break;
    }
    fmt::print("\n");
  }
}