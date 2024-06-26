#include <fmt/core.h>
#include <fmt/format.h>
#include <fstream>
#include <string_view>

constexpr static std::string_view insn_names[] = {
  "halt",
  "ld", "st",
  "add", "sub", "mul", "div", "mod", "equ", "gt", "lt",
  "jmp", "jif",
};

// Either an immediate value or a register ID,
// depending on the lowest bit - an encoding used by many instructions
struct Imm_or_reg { uint32_t encoded; };

template<> struct fmt::formatter<Imm_or_reg> {
  static constexpr auto parse(fmt::format_parse_context& ctx) { return ctx.begin(); }
  static appender format(Imm_or_reg src, format_context& ctx) {
    if (src.encoded & 1)
      return format_to(ctx.out(), "r{}", src.encoded >> 1);
    else
      return format_to(ctx.out(), "{:#x}", src.encoded >> 1);
  }
};

int main(int argc, char** argv) {
  if (argc != 2) {
    fmt::print(stderr, "Usage: {} <image-name>\n", argv[0]);
    return 1;
  }

  const char* filename = argv[1];
  std::ifstream f(filename, std::ios::binary);
  if (!f) {
    fmt::print(stderr, "Cannot open '{}'\n", filename);
    return 2;
  }

  uint32_t insn;
  uint32_t addr = 0;
  for (; f.read(reinterpret_cast<char*>(&insn), sizeof(insn)); addr++) {
    const uint8_t opcode = insn & 0xF;

    const auto fmt_operands = [&] () -> std::string {
      switch (opcode) {
      case 0x0:
        return fmt::format("{:#x}", insn >> 4);
      case 0x1:
      case 0x2:
        return fmt::format(
          "r{}, mem[{}]",
          (insn >> 4) & 0x3F,
          Imm_or_reg(insn >> 10)
        );
      case 0xB:
        return fmt::format("{:#x}", insn >> 4);
      case 0xC:
        return fmt::format("r{}, {:#x}", (insn >> 4) & 0x3F, insn >> 10);
      default:
        return fmt::format(
          "r{}, {}, {}",
          (insn >> 4) & 0x3F,
          Imm_or_reg((insn >> 10) & 0x7FF),
          Imm_or_reg(insn >> 21)
        );
      }
    };

    fmt::print("{:3x}: {:#010x} ", addr, insn);
    if (opcode < 0xD)
      fmt::print("{} {}\n", insn_names[opcode], fmt_operands());
    else
      fmt::print("???\n", insn);
  }
}