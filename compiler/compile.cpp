#include "compile.hpp"
#include "diag.hpp"
#include <algorithm>
#include <optional>
#include <span>
#include <unordered_map>

namespace ac {

static uint32_t next_variable_id = 0;
static std::vector<uint32_t> static_data;
static std::unordered_map<std::string_view, Variable_id> variables;

static std::vector<Instruction> code;

static Variable_id create_variable() {
  ++next_variable_id;
  if (next_variable_id == 0)
    error("Internal compiler error: too many variables created");
  return Variable_id(next_variable_id);
}


// We can use emit_unpatched_jump and patch_jump_to_here
// when creating forward jumps, for example:
// {
//   auto my_fwd_jump = emit_unpatched_jump();
//   emit_some_more_code();
//   patch_jump_to_here(my_fwd_jump);
// }

using Jump_id = size_t;
static constexpr int32_t unpatched_jump_magic = 0xDEAD;

static Jump_id emit_unpatched_jump(Value condition = Constant(true)) {
  size_t result = code.size();
  code.push_back({
    .op = Operation::jump,
    .dest = {},
    .operand1 = condition,
    .operand2 = Constant(unpatched_jump_magic),
  });
  return result;
}

static void patch_jump_to_here(Jump_id id) {
  int32_t& value = code[id].operand2.as<Constant>().value;
  assert(value == unpatched_jump_magic);
  value = (int32_t) code.size();
}


static Variable_id emit_arith
(Operation op, Variable_id dest, Value operand1, Value operand2) {
  code.push_back({
    .op = op,
    .dest = dest,
    .operand1 = operand1,
    .operand2 = operand2,
  });
  return dest;
}

static Variable_id emit_mov(Variable_id dest, Value src) {
  code.push_back({
    .op = Operation::mov,
    .dest = dest,
    .operand1 = src,
    .operand2 = {},
  });
  return dest;
}

static Variable_id emit_set_variable(std::string_view name, Ast::Node& value) {
  auto [it, is_new] = variables.try_emplace(name);
  Variable_id& dest = it->second;
  if (is_new)
    dest = create_variable();
  return emit_mov(dest, compile_node(value));
}

static Value emit_if(Ast::Node& cond_expr, Ast::Node& then_expr, Ast::Node& else_expr) {
  Variable_id result = create_variable();

  auto jump_toward_else = emit_unpatched_jump(compile_node(cond_expr));
  emit_mov(result, compile_node(then_expr));
  auto jump_toward_end = emit_unpatched_jump();

  patch_jump_to_here(jump_toward_else);
  emit_mov(result, compile_node(else_expr));

  patch_jump_to_here(jump_toward_end);
  return result;
}


Value compile_node(Ast::Node& node) {
  return node.match(
    [] (Ast::Identifier& ident) -> Value {
      auto it = variables.find(ident.name);
      if (it == variables.end())
        error("No variable named '{}' was declared", ident.name);
      return Variable_id(it->second);
    },
    [] (Ast::Number number) -> Value {
      return Constant(number.value);
    },
    [] (Ast::String& string) -> Value {
      static_data.reserve(static_data.size() + 1 + string.value.size());
      uint32_t address = static_data.size();
      static_data.push_back(string.value.size());
      for (char c: string.value)
        static_data.push_back(c);
      return Constant(static_cast<int32_t>(address));
    },
    [] (Ast::Parens& parens) -> Value {
      return compile_parens(parens);
    }
  );
}

static std::optional<Value> maybe_emit_intrinsic
(std::string_view func_name, std::span<Ast::Node> args) {
  if (func_name == "set") {
    // Set a variable to a value, and return this value
    if (args.size() != 2 || !args[0].is<Ast::Identifier>())
      error("Syntax: (set var-name expression)");
    return emit_set_variable(args[0].as<Ast::Identifier>().name, args[1]);
  } else if (func_name == "if") {
    // (if x y z)
    if (args.size() != 3)
      error("Syntax: (if cond-expr then-expr else-expr)");
    return emit_if(args[0], args[1], args[2]);
  }
  return std::nullopt;
}

static std::optional<Value> maybe_emit_nary
(std::string_view func_name, std::span<const Value> inputs) {
  std::optional<Operation> op;
  if (func_name == "+")
    op = Operation::add;
  else if (func_name == "*")
    op = Operation::mul;

  if (!op)
    return std::nullopt;

  if (inputs.size() < 2)
    error("'{}' needs at least 2 arguments, got {}", func_name, inputs.size());

  Value latest = inputs[0];
  for (size_t i = 1; i < inputs.size(); i++)
    latest = emit_arith(*op, create_variable(), latest, inputs[i]);
  return latest;
}

static std::optional<Value> maybe_emit_binop
(std::string_view func_name, std::span<const Value> inputs) {
  std::optional<Operation> op;
  if (func_name == "-")
    op = Operation::sub;
  else if (func_name == "/")
    op = Operation::div;
  else if (func_name == "%")
    op = Operation::mod;

  if (!op)
    return std::nullopt;

  if (inputs.size() != 2)
    error("'{}' needs 2 arguments, got {}", func_name, inputs.size());

  return emit_arith(*op, create_variable(), inputs[0], inputs[1]);
}


Value compile_parens(Ast::Parens& expr) {
  auto& terms = expr.children;
  assert(!terms.empty()); // Should have been a parse error

  auto& func = terms[0];
  auto arguments = std::span{ terms.begin() + 1, terms.end() };

  if (!func.is<Ast::Identifier>())
    error("Function name must be an identifier (indirect calls are not supported)");
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

  if (func_name == "progn") {
    if (inputs.empty())
      error("'{}' needs at least one argument", func_name);
    return inputs.back();
  }

  error("'{}' is not a known function", func_name);
}

std::span<Instruction> get_emitted_code() {
  return code;
}

} // namespace ac