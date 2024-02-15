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
constexpr int num_registers = 29;


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
struct Memory_addr { uint16_t addr; }; // Will need patching after for relocation
struct Register_id { uint8_t id; };
using Variable_location = One_of<Memory_addr, Register_id>;

std::vector<Variable_location> color_variables(std::span<Lifetime> lives) {
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
  std::vector<Variable_location> result(lives.size());
  int16_t next_memory_addr = 0;

  for (int i = 0; i < lives.size(); i++) {
    int our_id = vars_by_life_length[i];
    auto& our_life = lives[our_id];
    bool taken[num_registers] = {};

    for (int j = 0; j < i; j++) {
      int their_id = vars_by_life_length[j];
      auto& their_life = lives[their_id];
      if (auto their_reg = result[their_id].maybe_as<Register_id>();
          their_reg
          && our_life.end >= their_life.start
          && our_life.start <= their_life.end)
        taken[their_reg->id] = true;
    }

    bool found_register = false;
    for (int reg_id = 0; reg_id < num_registers; reg_id++) {
      if (!taken[reg_id]) {
        result[our_id] = Register_id(reg_id);
        found_register = true;
        break;
      }
    }
    if (!found_register)
      result[our_id] = Memory_addr(next_memory_addr++);
  }

  return result;
}


// ===========================================================================
// Memory-aware codegen.

// Much of the same operations as the abstract `IR_op`, but
// without mov (`add X, 0` suffices) and with loads and stores

enum class Hw_opcodes: uint8_t {
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
  cmp_lt = 0xa,
  cond_jump = 0xb,
};

struct Codegen_state {
  std::vector<uint32_t> static_data;
  std::vector<uint32_t> hw_code;

  void emit_halt() {
    hw_code.push_back(0);
  }
};

} // anon namespace


void emit_image(std::ostream& os, IR_output&& cc) {
  (void) os;

  Codegen_state codegen;

  auto code = std::move(cc.code);
  codegen.static_data = std::move(cc.data);

  auto lifetimes = build_var_lifetimes(cc.num_variables, code);
  auto coloring = color_variables(lifetimes);

  fmt::print("{} variables colored onto {} registers:\n", cc.num_variables, num_registers);
  for (int var_id = 0; var_id < cc.num_variables; var_id++) {
    fmt::print("  #{} -> ", var_id);
    coloring[var_id].match(
      [] (Register_id reg) { fmt::print("reg #{}\n", reg.id); },
      [] (Memory_addr mem) { fmt::print("MEM #{}\n", mem.addr); }
    );
  }
}