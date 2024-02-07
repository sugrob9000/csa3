#include "compile.hpp"
#include "parse.hpp"
#include <fmt/core.h>
#include <iostream>

template<>
struct fmt::formatter<ac::Value> {
  constexpr auto parse(format_parse_context& ctx) { return ctx.begin(); }
  appender format(ac::Value& value, format_context& ctx) {
    return value.match(
      [&] (ac::Constant c) { return format_to(ctx.out(), "{:x}", c.value); },
      [&] (ac::Variable_id v) { return format_to(ctx.out(), "#{}", v.id); }
    );
  }
};

int main() {
  auto ast = Ast::parse_stream(std::cin);
  for (auto& expr: ast.toplevel_exprs)
    ac::compile_parens(expr);

  for (int i = 0; auto& insn: ac::get_emitted_code()) {
    constexpr std::string_view insn_names[] = {
      "mov", "add", "sub", "mul", "div", "mod", "jmp",
    };

    fmt::print("{:4}: ", i);

    switch (insn.op) {
      using enum ac::Operation;
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
    i++;
  }
}