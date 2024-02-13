#include "codegen.hpp"
#include "compile.hpp"
#include "parse.hpp"
#include <fmt/core.h>
#include <fmt/format.h>
#include <iostream>

template<>
struct fmt::formatter<Value> {
  constexpr auto parse(format_parse_context& ctx) { return ctx.begin(); }
  appender format(Value& value, format_context& ctx) {
    return value.match(
      [&] (Constant c) { return format_to(ctx.out(), "{:x}", c.value); },
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
      "mov", "add", "sub", "mul", "div", "mod", "jmp",
    };

    fmt::print("{:4x}: ", i++);

    switch (insn.op) {
      using enum Operation;
    case jump:
      fmt::print("jmp -> {} if {}\n", insn.operand2, insn.operand1);
      break;
    case mov:
      fmt::print("mov #{} <- {}\n", insn.dest.id, insn.operand1);
      break;
    default:
      fmt::print("{}: #{} <- {} {}\n",
          insn_names[static_cast<int>(insn.op)],
          insn.dest.id,
          insn.operand1,
          insn.operand2);
      break;
    }
  }

  emit_image(std::cout, compiler_output);
}