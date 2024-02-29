#include "diagnostics.hpp"
#include "stages.hpp"
#include <algorithm>
#include <optional>
#include <span>
#include <unordered_map>

namespace {

constexpr uint32_t mmio_addr = 0x3;

struct Compiler {
  // The eventual output of this stage
  std::vector<uint32_t> static_data;
  std::vector<Ir::Insn> emitted_code;

  // This stage liberally creates new abstract "variables"
  int next_variable_id = 0;
  Ir::Variable new_var() {
    return Ir::Variable(next_variable_id++);
  }

  // All incoming string data outlives the compiler, so we can store just views
  std::unordered_map<std::string_view, Ir::Variable> variables;


  // =========================================================================
  // Emitting single instructions.
  // Straightforward wrappers around emitted_code.push_back(),
  // but it's very convenient to return the destination variable

  Ir::Variable emit(Ir::Op op, Ir::Variable dest, Ir::Value src1, Ir::Value src2) {
    emitted_code.push_back({
      .op = op,
      .dest = dest,
      .src1 = src1,
      .src2 = src2,
    });
    return dest;
  }

  Ir::Variable emit_mov(Ir::Variable dest, Ir::Value src) {
    return emit(Ir::Op::mov, dest, src, {});
  }

  Ir::Variable emit_load(Ir::Variable dest, Ir::Value addr) {
    return emit(Ir::Op::load, dest, addr, {});
  }

  Ir::Value emit_store(Ir::Value value, Ir::Value addr) {
    (void) emit(Ir::Op::store, {}, addr, value);
    return value;
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

  void emit_jump_to(Label label, Ir::Value condition = Ir::Constant(1)) {
    emit(Ir::Op::jump, {}, condition, Ir::Constant(label));
  }

  using Jump_id = size_t;

  Jump_id emit_unpatched_jump(Ir::Value condition = Ir::Constant(1)) {
    size_t result = emitted_code.size();
    emit(Ir::Op::jump, {}, condition, Ir::Constant(unpatched_jump_magic));
    return result;
  }

  void patch_jump_to_here(Jump_id id) {
    int32_t& value = emitted_code[id].src2.as<Ir::Constant>().value;
    assert(value == unpatched_jump_magic);
    value = label_here();
  }

  // =========================================================================
  // Emitting intrinsics.
  // They must take AST nodes and not values, because they contain logic
  // as to what gets evaluated or not.

  Ir::Variable emit_set(std::string_view name, Ast::Node& value) {
    auto [it, is_new] = variables.try_emplace(name);
    Ir::Variable& dest = it->second;
    if (is_new)
      dest = new_var();
    return emit_mov(dest, compile_node(value));
  }

  Ir::Variable emit_if(Ast::Node& cond_expr, Ast::Node& then_expr, Ast::Node& else_expr) {
    Ir::Variable result = new_var();

    auto jump_to_then = emit_unpatched_jump(compile_node(cond_expr));
    emit_mov(result, compile_node(else_expr));
    auto jump_to_end = emit_unpatched_jump();

    patch_jump_to_here(jump_to_then);
    emit_mov(result, compile_node(then_expr));

    patch_jump_to_here(jump_to_end);
    return result;
  }

  Ir::Constant emit_while(Ast::Node& cond_expr, Ast::Node& loop_expr) {
    auto top = label_here();
    auto cond = compile_node(cond_expr);
    auto inverse_cond = emit(Ir::Op::cmp_equ, new_var(), cond, Ir::Constant(0));
    auto jump_to_end = emit_unpatched_jump(inverse_cond);
    (void) compile_node(loop_expr);
    emit_jump_to(top);
    patch_jump_to_here(jump_to_end);
    return Ir::Constant(0);
  }

  std::optional<Ir::Value> maybe_emit_intrinsic
  (std::string_view func_name, std::span<Ast::Node> args) {
    if (func_name == "set") {
      // Set a variable to a value, and return this value
      if (args.size() != 2 || !args[0].is<Ast::Identifier>())
        error("Syntax: (set var-name expression)");
      return emit_set(args[0].as<Ast::Identifier>().name, args[1]);
    } else if (func_name == "if") {
      // Depending on the condition, only evaluate one of the arguments
      if (args.size() != 3)
        error("Syntax: (if COND-EXPR THEN-EXPR ELSE-EXPR)");
      return emit_if(args[0], args[1], args[2]);
    } else if (func_name == "while") {
      // Evaluate loop-expr, always return 0
      if (args.size() != 2)
        error("Syntax: (while COND-EXPR LOOP-EXPR)");
      return emit_while(args[0], args[1]);
    } else if (func_name == "alloc-static") {
      if (args.size() != 1 || !args[0].is<Ast::Number>())
        error("Syntax: (alloc-static CONSTANT-AMOUNT)");
      auto address = int32_t(static_data.size());
      static_data.resize(static_data.size() + args[0].as<Ast::Number>().value);
      return Ir::Constant(address);
    }
    return std::nullopt;
  }

  // =========================================================================
  // Emitting builtins.
  // They unconditionally evaluate all arguments,
  // and we know what code to generate for them.

  std::optional<Ir::Value> maybe_emit_lassoc
  (std::string_view func_name, std::span<const Ir::Value> inputs) {
    std::optional<Ir::Op> op;
    if (func_name == "+")
      op = Ir::Op::add;
    else if (func_name == "*")
      op = Ir::Op::mul;

    if (!op)
      return std::nullopt;

    if (inputs.size() < 2)
      error("'{}' needs at least 2 arguments, got {}", func_name, inputs.size());

    Ir::Value latest = inputs[0];
    for (size_t i = 1; i < inputs.size(); i++)
      latest = emit(*op, new_var(), latest, inputs[i]);
    return latest;
  }

  std::optional<Ir::Value> maybe_emit_binop
  (std::string_view func_name, std::span<const Ir::Value> inputs) {
    std::optional<Ir::Op> op;

    if (func_name == "-")
      op = Ir::Op::sub;
    else if (func_name == "/")
      op = Ir::Op::div;
    else if (func_name == "%")
      op = Ir::Op::mod;
    else if (func_name == "=")
      op = Ir::Op::cmp_equ;
    else if (func_name == ">")
      op = Ir::Op::cmp_gt;
    else if (func_name == "<")
      op = Ir::Op::cmp_lt;

    if (!op)
      return std::nullopt;

    if (inputs.size() != 2)
      error("'{}' needs 2 arguments, got {}", func_name, inputs.size());

    return emit(*op, new_var(), inputs[0], inputs[1]);
  }

  Ir::Constant emit_print_str(Ir::Value str) {
    Ir::Variable counter = emit_load(new_var(), str);
    Ir::Variable pointer = emit(Ir::Op::add, new_var(), str, Ir::Constant(1));

    Ir::Variable is_zero = emit(Ir::Op::cmp_equ, new_var(), counter, Ir::Constant(0));
    Jump_id skip_loop_jump = emit_unpatched_jump(is_zero);

    Label top = label_here();
    Ir::Variable character = emit_load(new_var(), pointer);
    emit_store(character, Ir::Constant(mmio_addr));

    Ir::Variable tmp = new_var();
    emit(Ir::Op::add, tmp, pointer, Ir::Constant(1));
    emit_mov(pointer, tmp);

    emit(Ir::Op::sub, tmp, counter, Ir::Constant(1));
    emit_mov(counter, tmp);
    emit_jump_to(top, counter);

    patch_jump_to_here(skip_loop_jump);
    return Ir::Constant(0);
  }

  // =========================================================================
  // Compilation of high-level langauge constructs.

  Ir::Value compile_node(Ast::Node& node) {
    return node.match(
      [&] (Ast::Identifier& ident) -> Ir::Value {
        auto it = variables.find(ident.name);
        if (it == variables.end())
          error("No variable named '{}' was declared", ident.name);
        return Ir::Variable(it->second);
      },
      [&] (Ast::Number number) -> Ir::Value {
        return Ir::Constant(number.value);
      },
      [&] (Ast::String& string) -> Ir::Value {
        static_data.reserve(static_data.size() + 1 + string.value.size());
        uint32_t address = static_data.size();
        static_data.push_back(string.value.size());
        for (char c: string.value)
          static_data.push_back(c);
        return Ir::Constant(static_cast<int32_t>(address));
      },
      [&] (Ast::Parens& parens) -> Ir::Value {
        return compile_parens(parens);
      }
    );
  }

  Ir::Value compile_parens(Ast::Parens& expr) {
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
    std::vector<Ir::Value> inputs;
    inputs.reserve(arguments.size());
    for (auto& arg: arguments)
      inputs.push_back(compile_node(arg));

    if (auto binop = maybe_emit_binop(func_name, inputs))
      return *binop;

    if (auto lassoc = maybe_emit_lassoc(func_name, inputs))
      return *lassoc;

    // Kind of intrinsics, but these do evaluate all their arguments
    if (func_name == "progn") {
      if (inputs.empty())
        error("progn needs at least one argument");
      return inputs.back();
    } else if (func_name == "read-mem") {
      if (inputs.size() != 1)
        error("Syntax: (read-mem ADDR)");
      return emit_load(new_var(), inputs[0]);
    } else if (func_name == "write-mem") {
      if (inputs.size() != 2)
        error("Syntax: (write-mem ADDR VALUE)");
      return emit_store(inputs[1], inputs[0]);
    } else if (func_name == "print-str") {
      if (inputs.size() != 1)
        error("print-str needs exactly one argument");
      return emit_print_str(inputs[0]);
    }

    error("'{}' is not a known function", func_name);
  }
};

} // anon namespace

Ir Ir::compile(Ast& ast) {
  Compiler compiler;

  // - Reserve a word at 0x0 for a jump to the code
  // - Reserve 2 more words to guard MMIO against prefetch
  // - Reserve a word at 0x3 for MMIO
  compiler.static_data.resize(4);
  assert(mmio_addr < compiler.static_data.size());

  for (auto& expr: ast.sexprs)
    compiler.compile_parens(expr);

  // Add a final halt
  compiler.emit(Ir::Op::halt, {}, {}, {});

  return {
    .code = std::move(compiler.emitted_code),
    .data = std::move(compiler.static_data),
    .num_variables = compiler.next_variable_id,
  };
}

bool Ir::Insn::has_valid_dest() const {
  return op != Ir::Op::halt
      && op != Ir::Op::jump
      && op != Ir::Op::store;
}

bool Ir::Insn::has_valid_src1() const {
  return op != Ir::Op::halt;
}

bool Ir::Insn::has_valid_src2() const {
  return op != Ir::Op::halt
      && op != Ir::Op::mov
      && op != Ir::Op::load;
}