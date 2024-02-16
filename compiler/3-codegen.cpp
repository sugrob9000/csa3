#include "diagnostics.hpp"
#include "stages.hpp"
#include <algorithm>
#include <fmt/core.h>
#include <iostream>
#include <numeric>
#include <span>

namespace {

// ===========================================================================

bool has_dest(IR_op op) {
  return op != IR_op::halt
      && op != IR_op::jump;
}

bool has_src1(IR_op op) {
  return op != IR_op::halt;
}

bool has_src2(IR_op op) {
  return op != IR_op::halt
      && op != IR_op::mov;
}

// ===========================================================================
// Register coloring.

// 2 registers are reserved for loads of spilled values and stores thereto.
// An instruction might have had both its source operands spilled, so we need 2.
constexpr int num_registers = 4; // 64, but we want to test spills
struct Register_id { uint8_t id; };
constexpr Register_id scratch_reg1 = { 62 };
//constexpr Register_id scratch_reg2 = { 63 };


// A variable is considered alive between its first and last usage, inclusive
struct Lifetime {
  int start = std::numeric_limits<int>::max();
  int end = std::numeric_limits<int>::min();
};

auto build_var_lifetimes(int num_variables, std::span<IR_insn> code) {
	std::vector<Lifetime> result(num_variables);

  for (int insn_id = 0; insn_id < code.size(); insn_id++) {
    const auto maybe_update_lifetime = [&] (Value v) {
      if (v.is<Variable_id>()) {
				auto& life = result[v.as<Variable_id>().id];
				life.start = std::min(life.start, insn_id);
				life.end = std::max(life.end, insn_id);
      }
    };
    auto& insn = code[insn_id];
    if (has_dest(insn.op)) maybe_update_lifetime(insn.dest);
    if (has_src1(insn.op)) maybe_update_lifetime(insn.src1);
    if (has_src2(insn.op)) maybe_update_lifetime(insn.src2);
  }

	return result;
}

// The places a variable can be assigned to live in
struct Memory_addr { uint32_t addr; }; // Will need patching after for relocation
using Variable_location = One_of<Memory_addr, Register_id>;

struct Coloring_result {
  std::vector<Variable_location> locs;
  int num_spilled_variables;
};

Coloring_result color_variables(std::span<Lifetime> lives) {
  // Sort variables by ascending length of life, putting "hot" ones first
  std::vector<int> vars_by_life_length(lives.size());
  for (int i = 0; i < lives.size(); i++)
    vars_by_life_length[i] = i;
  std::sort(vars_by_life_length.begin(), vars_by_life_length.end(),
    [&] (int a, int b) {
      return (lives[a].end - lives[a].start)
           < (lives[b].end - lives[b].start);
    });

  // Greedily assign a register to each variable. "Hot" variables will go first,
  // becoming more likely to grab registers (and because their life is shorter,
  // being less likely to interfere with others).
  Coloring_result result = {
    .locs = std::vector<Variable_location>(lives.size()),
    .num_spilled_variables = 0
  };
  int32_t next_memory_addr = 0;

  for (int i = 0; i < lives.size(); i++) {
    int our_id = vars_by_life_length[i];
    auto& our_life = lives[our_id];
    bool taken[num_registers] = {};

    for (int j = 0; j < i; j++) {
      int their_id = vars_by_life_length[j];
      auto& their_life = lives[their_id];
      auto their_reg = result.locs[their_id].maybe_as<Register_id>();
      if (their_reg
      && our_life.end >= their_life.start
      && our_life.start <= their_life.end)
        taken[their_reg->id] = true;
    }

    bool found_register = false;
    for (int reg_id = 0; reg_id < num_registers; reg_id++) {
      if (!taken[reg_id]) {
        result.locs[our_id] = Register_id(reg_id);
        found_register = true;
        break;
      }
    }
    if (!found_register)
      result.locs[our_id] = Memory_addr(next_memory_addr++);
  }

  return result;
}


// ===========================================================================
// Memory-aware codegen.

// Most of the same operations as the abstract `IR_op`, but
// without mov (`add X, 0` suffices) and with loads and stores

enum class Hw_opcode: uint8_t {
  halt = 0x0,
  load = 0x1,
  store = 0x2,
  add = 0x3,
  sub = 0x4,
  mul = 0x5,
  div = 0x6,
  mod = 0x7,
  cmp_equ = 0x8,
  cmp_gt = 0x9,
  cmp_lt = 0xA,
  jump_if = 0xB,
};

struct Immediate { uint32_t value; };
using Hw_value = One_of<Register_id, Immediate>;

struct Codegen {
  std::vector<uint32_t> static_data;
  std::vector<uint32_t> hw_code;
  Coloring_result coloring;

  // Coloring gave us 0-based memory addresses for spilled variables,
  // but we have other data, so relocate them on top of our static data
  void relocate_variable_homes() {
    size_t old_top = static_data.size();
    static_data.resize(old_top + coloring.num_spilled_variables);
    for (auto& loc: coloring.locs) {
      if (auto mem = loc.maybe_as<Memory_addr>())
        mem->addr += old_top;
    }
  }

  bool is_spilled(Variable_id var) {
    return coloring.locs[var.id].is<Memory_addr>();
  }

  // Can only be called with external knowledge of validity
  Register_id reg_of(Variable_id var) { return coloring.locs[var.id].as<Register_id>(); }
  Memory_addr addr_of(Variable_id var) { return coloring.locs[var.id].as<Memory_addr>(); }

  void emit_halt() {
    hw_code.push_back(0);
  }

  void emit_mem_imm(Hw_opcode op, Register_id reg, Memory_addr mem) {
    assert(op == Hw_opcode::load || op == Hw_opcode::store);
    if (mem.addr >= (1u << 21))
      error("Tried to access absolute immediate address {} -- too large", mem.addr);
    hw_code.push_back(
      static_cast<uint32_t>(op) |
      (reg.id << 4) |
      (1u << 10) |
      (mem.addr << 11)
    );
  }

  void emit_binop(Hw_opcode op, Register_id dest, Hw_value src1, Hw_value src2) {
    const auto op_id = static_cast<uint32_t>(op);
    assert(op_id >= 0x3 && op_id <= 0xA);
    const auto convert_src = [] (Hw_value src) -> uint32_t {
      return src.match(
        [] (Register_id reg) { return 1u | (reg.id << 1); },
        [] (Immediate imm) {
          assert(imm.value < (1u << 10));
          return imm.value << 1;
        }
      );
    };
    hw_code.push_back(
      op_id |
      (dest.id << 4) |
      (convert_src(src1) << 10) |
      (convert_src(src2) << 21)
    );
  }

  void emit_jump(Hw_opcode op, Register_id condition, uint32_t dest) {
    assert(op == Hw_opcode::jump_if);
    if (dest >= (1u << 22))
      error("Tried to jump to absolute immediate address {} -- too large", dest);
    hw_code.push_back(
      static_cast<uint32_t>(op) |
      (condition.id << 4) |
      (dest << 10)
    );
  }

  void handle_mov(Variable_id dest, Value src) {
    //     --- Situation ---    --- What do ---
    // 1.  reg <- reg           add R,0
    // 2.  reg <- mem           load
    // 3.  reg <- small const   add I,0
    // 4.  reg <- large const   load
    // 5.  mem <- reg           store
    // 6.  mem <- mem           load + store
    // 7.  mem <- small const   add I,0 + store
    // 8.  mem <- large const   load + store

    enum Value_kind { reg = 0, mem = 1, small_const = 2, large_const = 3 };
    const Value_kind dest_kind = is_spilled(dest) ? mem : reg;
    const Value_kind src_kind = src.match(
      [&] (Variable_id var) { return is_spilled(var) ? mem : reg; },
      [] (Constant c) { return (c.value >= (1u << 10)) ? large_const : small_const; }
    );

    const auto src_reg = [&] { return reg_of(src.as<Variable_id>()); };
    const auto src_addr = [&] { return addr_of(src.as<Variable_id>()); };
    const auto src_const = [&] { return src.as<Constant>().value; };

    Memory_addr src_spilled_addr{};
    if (src_kind == large_const) {
      // Stash the constant away if needed
      src_spilled_addr = Memory_addr(static_data.size());
      static_data.push_back(src_const());
    }

    int situation = 1 + src_kind + 4 * dest_kind; // see comment at top of fn
    switch (situation) {
      using enum Hw_opcode;
    case 1: return emit_binop(add, reg_of(dest), src_reg(), Immediate(0));
    case 2: return emit_mem_imm(load, reg_of(dest), src_addr());
    case 3: return emit_binop(add, reg_of(dest), Immediate(src_const()), Immediate(0));
    case 4: return emit_mem_imm(load, reg_of(dest), src_spilled_addr);
    case 5: return emit_mem_imm(store, src_reg(), addr_of(dest));
    case 6:
      emit_mem_imm(load, scratch_reg1, src_addr());
      emit_mem_imm(store, scratch_reg1, addr_of(dest));
      return;
    case 7:
      emit_binop(add, scratch_reg1, Immediate(src_const()), Immediate(0));
      emit_mem_imm(store, scratch_reg1, addr_of(dest));
      return;
    case 8:
      emit_mem_imm(load, scratch_reg1, src_spilled_addr);
      emit_mem_imm(store, scratch_reg1, addr_of(dest));
      return;
    default: assert(false);
    }
  }

  void handle_ir_insn(const IR_insn& insn) {
    switch (insn.op) {
      using enum IR_op;
    case halt: return emit_halt();
    case mov: return handle_mov(insn.dest, insn.src1);
    default: break;
    }
  }
};

} // anon namespace


void emit_image(std::ostream& os, IR_output&& cc) {
  Codegen codegen;

  auto code = std::move(cc.code);
  codegen.static_data = std::move(cc.data);

  {
    auto lifetimes = build_var_lifetimes(cc.num_variables, code);
    codegen.coloring = color_variables(lifetimes);
    codegen.relocate_variable_homes();

    fmt::print("{} variables colored onto {} registers:\n", cc.num_variables, num_registers);
    for (int var_id = 0; var_id < cc.num_variables; var_id++) {
      fmt::print("  #{} -> ", var_id);
      codegen.coloring.locs[var_id].match(
        [] (Register_id reg) { fmt::print("reg #{}\n", reg.id); },
        [] (Memory_addr mem) { fmt::print("MEM #{}\n", mem.addr); }
      );
    }
  }

  for (IR_insn& insn: code)
    codegen.handle_ir_insn(insn);

  fmt::print("HW codegen:\n");
  for (uint32_t hw_insn: codegen.hw_code)
    fmt::print("  0x{:08x}\n", hw_insn);

  (void) os;
}
