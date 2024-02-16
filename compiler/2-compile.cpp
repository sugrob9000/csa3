#include "diagnostics.hpp"
#include "stages.hpp"
#include <algorithm>
#include <optional>
#include <span>
#include <unordered_map>

namespace {

struct Compiler {
  // The eventual output of this stage
  std::vector<uint32_t> static_data;
  std::vector<IR_insn> emitted_code;

  // This stage liberally creates new abstract "variables"
  int next_variable_id = 0;
  Variable_id new_var() {
    return Variable_id(next_variable_id++);
  }

  // All incoming string data outlives the compiler, so we can store just views
  std::unordered_map<std::string_view, Variable_id> variables;


  // =========================================================================
  // Emitting single instructions.
  // Straightforward wrappers around emitted_code.push_back(),
  // but it's very convenient to return the destination variable

  Variable_id emit(IR_op op, Variable_id dest, Value src1, Value src2) {
    emitted_code.push_back({
      .op = op,
      .dest = dest,
      .src1 = src1,
      .src2 = src2,
    });
    return dest;
  }

  Variable_id emit_mov(Variable_id dest, Value src) {
    return emit(IR_op::mov, dest, src, {});
  }

  // =========================================================================
  // Emitting jumps.
  // With forward jumps, we first emit the jump, then later its label:
  //
  //   auto my_fwd_jump = emit_unpatched_jump();
  //   emit_some_more_code();
  //   patch_jump_to_here(my_fwd_jump);
  //
  // With backward jumps, we first emit the label, then later the jump:
  //
  //   auto my_label = label_here();
  //   emit_some_more_code();
  //   emit_jump_to(my_label);

  using Label = int32_t;
  constexpr static Label unpatched_jump_magic = 0x7FFFDEAD;

  Label label_here() {
    return Label(emitted_code.size());
  }

  void emit_jump_to(Label label, Value condition = Constant(true)) {
    emit(IR_op::jump, {}, condition, Constant(label));
  }

  using Jump_id = size_t;

  Jump_id emit_unpatched_jump(Value condition = Constant(true)) {
    size_t result = emitted_code.size();
    emit(IR_op::jump, {}, condition, Constant(unpatched_jump_magic));
    return result;
  }

  void patch_jump_to_here(Jump_id id) {
    int32_t& value = emitted_code[id].src2.as<Constant>().value;
    assert(value == unpatched_jump_magic);
    value = label_here();
  }

  // =========================================================================
  // Emitting intrinsics.
  // They must take AST nodes and not values, because they contain logic
  // as to what gets evaluated or not.

  Variable_id emit_set(std::string_view name, Ast::Node& value) {
    auto [it, is_new] = variables.try_emplace(name);
    Variable_id& dest = it->second;
    if (is_new)
      dest = new_var();
    return emit_mov(dest, compile_node(value));
  }

  Variable_id emit_if(Ast::Node& cond_expr, Ast::Node& then_expr, Ast::Node& else_expr) {
    Variable_id result = new_var();

    auto jump_to_then = emit_unpatched_jump(compile_node(cond_expr));
    emit_mov(result, compile_node(else_expr));
    auto jump_to_end = emit_unpatched_jump();

    patch_jump_to_here(jump_to_then);
    emit_mov(result, compile_node(then_expr));

    patch_jump_to_here(jump_to_end);
    return result;
  }

  Constant emit_while(Ast::Node& cond_expr, Ast::Node& loop_expr) {
    auto top = label_here();
    auto cond = compile_node(cond_expr);
    auto inverse_cond = emit(IR_op::cmp_equ, new_var(), cond, Constant(0));
    auto jump_to_end = emit_unpatched_jump(inverse_cond);
    (void) compile_node(loop_expr);
    emit_jump_to(top);
    patch_jump_to_here(jump_to_end);
    return Constant(0);
  }

  std::optional<Value> maybe_emit_intrinsic
  (std::string_view func_name, std::span<Ast::Node> args) {
    if (func_name == "set") {
      // Set a variable to a value, and return this value
      if (args.size() != 2 || !args[0].is<Ast::Identifier>())
        error("Syntax: (set var-name expression)");
      return emit_set(args[0].as<Ast::Identifier>().name, args[1]);
    } else if (func_name == "if") {
      // Depending on the condition, only evaluate one of the arguments
      if (args.size() != 3)
        error("Syntax: (if cond-expr then-expr else-expr)");
      return emit_if(args[0], args[1], args[2]);
    } else if (func_name == "while") {
      // Evaluate loop-expr, always return 0
      if (args.size() != 2)
        error("Syntax: (while cond-expr loop-expr)");
      return emit_while(args[0], args[1]);
    }
    return std::nullopt;
  }

  // =========================================================================
  // Emitting builtins.
  // They unconditionally evaluate all arguments,
  // and we know what code to generate for them.

  std::optional<Value> maybe_emit_nary
  (std::string_view func_name, std::span<const Value> inputs) {
    std::optional<IR_op> op;
    if (func_name == "+")
      op = IR_op::add;
    else if (func_name == "*")
      op = IR_op::mul;

    if (!op)
      return std::nullopt;

    if (inputs.size() < 2)
      error("'{}' needs at least 2 arguments, got {}", func_name, inputs.size());

    Value latest = inputs[0];
    for (size_t i = 1; i < inputs.size(); i++)
      latest = emit(*op, new_var(), latest, inputs[i]);
    return latest;
  }

  std::optional<Value> maybe_emit_binop
  (std::string_view func_name, std::span<const Value> inputs) {
    std::optional<IR_op> op;

    if (func_name == "-")
      op = IR_op::sub;
    else if (func_name == "/")
      op = IR_op::div;
    else if (func_name == "%")
      op = IR_op::mod;
    else if (func_name == "=")
      op = IR_op::cmp_equ;
    else if (func_name == ">")
      op = IR_op::cmp_gt;
    else if (func_name == "<")
      op = IR_op::cmp_lt;

    if (!op)
      return std::nullopt;

    if (inputs.size() != 2)
      error("'{}' needs 2 arguments, got {}", func_name, inputs.size());

    return emit(*op, new_var(), inputs[0], inputs[1]);
  }

  // =========================================================================
  // Compilation of high-level langauge constructs.

  Value compile_node(Ast::Node& node) {
    return node.match(
      [&] (Ast::Identifier& ident) -> Value {
        auto it = variables.find(ident.name);
        if (it == variables.end())
          error("No variable named '{}' was declared", ident.name);
        return Variable_id(it->second);
      },
      [&] (Ast::Number number) -> Value {
        return Constant(number.value);
      },
      [&] (Ast::String& string) -> Value {
        static_data.reserve(static_data.size() + 1 + string.value.size());
        uint32_t address = static_data.size();
        static_data.push_back(string.value.size());
        for (char c: string.value)
          static_data.push_back(c);
        return Constant(static_cast<int32_t>(address));
      },
      [&] (Ast::Parens& parens) -> Value {
        return compile_parens(parens);
      }
    );
  }

  Value compile_parens(Ast::Parens& expr) {
    std::span terms = expr.children;
    assert(!terms.empty()); // Should have been a parse error

    auto& func = terms[0];
    auto arguments = terms.subspan(1);

    if (!func.is<Ast::Identifier>())
      error("Function name must be an identifier");
    std::string_view func_name = func.as<Ast::Identifier>().name;

    // Intrinsics need access to the AST, so check for them
    // before trying to evaluate arguments
    if (auto intrinsic = maybe_emit_intrinsic(func_name, arguments))
      return *intrinsic;

    // Evaluate arguments
    std::vector<Value> inputs;
    inputs.reserve(arguments.size());
    for (auto& arg: arguments)
      inputs.push_back(compile_node(arg));

    if (auto binop = maybe_emit_binop(func_name, inputs))
      return *binop;

    if (auto nary = maybe_emit_nary(func_name, inputs))
      return *nary;

    // Kind of an intrinsic, but it also just evaluates all arguments anyway
    if (func_name == "progn") {
      if (inputs.empty())
        error("'{}' needs at least one argument", func_name);
      return inputs.back();
    }

    error("'{}' is not a known function", func_name);
  }
};

} // anon namespace

IR_output compile(Ast& ast) {
  Compiler compiler;

  // Reserve 1 word at address 0x0 for an instruction that
  // jumps over data, because 0x0 is the entry point
  compiler.static_data.emplace_back();

  for (auto& expr: ast.toplevel_exprs)
    compiler.compile_parens(expr);

  compiler.emit(IR_op::halt, {}, {}, {});

  return {
    .code = std::move(compiler.emitted_code),
    .data = std::move(compiler.static_data),
    .num_variables = compiler.next_variable_id,
  };
}