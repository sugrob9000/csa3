#include "stages.hpp"
#include <fmt/core.h>
#include <fmt/format.h>
#include <iostream>

template<>
struct fmt::formatter<Value> {
  constexpr auto parse(format_parse_context& ctx) { return ctx.begin(); }
  appender format(Value& value, format_context& ctx) {
    return value.match(
      [&] (Constant c) { return format_to(ctx.out(), "0x{:x}", c.value); },
      [&] (Variable_id v) { return format_to(ctx.out(), "#{}", v.id); }
    );
  }
};

int main() {
  auto ast = Ast::parse_stream(std::cin);
  auto compiler_output = compile(ast);
  fmt::print("data: [{:x}]\n", fmt::join(compiler_output.data, ", "));

  for (int i = 0; auto& insn: compiler_output.code) {
    constexpr std::string_view insn_names[] = {
      "halt",
      "mov",
      "add", "sub", "mul", "div", "mod",
      "equ", "gt", "lt",
      "jump",
    };

    fmt::print("{:3x}: ", i++);

    switch (insn.op) {
      using enum IR_op;
    case jump:
      fmt::print("jmp -> {} if {}\n", insn.src2, insn.src1);
      break;
    case mov:
      fmt::print("mov #{} <- {}\n", insn.dest.id, insn.src1);
      break;
    case halt:
      fmt::print("halt\n");
      break;
    default:
      fmt::print("{}: #{} <- {}, {}\n",
          insn_names[static_cast<int>(insn.op)],
          insn.dest.id,
          insn.src1,
          insn.src2);
      break;
    }
  }

  emit_image(std::cout, std::move(compiler_output));
}
