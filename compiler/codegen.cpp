#include "codegen.hpp"
#include <algorithm>
#include <iostream>
#include <numeric>

namespace {

constexpr int num_regs = 4;

struct Reg_assignment {
  constexpr static int spilled = -1;
  std::vector<int> reg_ids;
};

struct Variable_assignment_info {
  // Auxillary info for assigning registers to variables
  constexpr static int life_low = std::numeric_limits<int>::min();
  constexpr static int life_high = std::numeric_limits<int>::max();

  int life_start = life_high;
  int life_end = life_low;
  int variable_id;
  int assigned_reg = Reg_assignment::spilled;

  bool is_spilled() const { return assigned_reg == Reg_assignment::spilled; }
  int life_length() const { return life_end - life_start; }

  bool lifetime_intersects(const Variable_assignment_info& other) const {
    return this->life_end >= other.life_start && this->life_start <= other.life_end;
  }

  void update_lifetime(int detected_use) {
    life_start = std::min(life_start, detected_use);
    life_end = std::max(life_end, detected_use);
  }

  void assert_valid_lifetime() const {
    assert(life_start != life_high);
    assert(life_end != life_low);
  }
};

Reg_assignment assign_regs(const Compiler_output& cc) {
  std::vector<Variable_assignment_info> vinfos(cc.num_variables);

  // Calculate the lifetime of each variable (from first to last use)
  for (int insn_id = 0; insn_id < cc.code.size(); insn_id++) {
    const auto maybe_update_lifetime = [&] (Value v) {
      if (v.is<Variable_id>())
        vinfos[v.as<Variable_id>().id].update_lifetime(insn_id);
    };
    auto& insn = cc.code[insn_id];
    maybe_update_lifetime(insn.dest);
    maybe_update_lifetime(insn.operand1);
    if (insn.op != Operation::jump && insn.op != Operation::mov)
      maybe_update_lifetime(insn.operand2);
  }

  for (int i = 0; i < cc.num_variables; i++) {
    vinfos[i].assert_valid_lifetime();
    vinfos[i].variable_id = i;
  }

  // Sort variables by ascending length of life, putting
  // "hot" variables first to let them grab registers
  std::sort(vinfos.begin(), vinfos.end(),
    [] (const auto& a, const auto& b) {
      return a.life_length() < b.life_length();
    }
  );

  // Greedily assign a register to each variable
  for (int vn1 = 0; vn1 < cc.num_variables; vn1++) {
    bool taken_regs[num_regs] = {};
    auto& us = vinfos[vn1];

    for (int vn2 = 0; vn2 < vn1; vn2++) {
      const auto& them = vinfos[vn2];
      const int their_reg = them.assigned_reg;
      if (their_reg != Reg_assignment::spilled
      && us.lifetime_intersects(them))
        taken_regs[their_reg] = true;
    }

    for (int reg_id = 0; reg_id < num_regs; reg_id++) {
      if (!taken_regs[reg_id]) {
        us.assigned_reg = reg_id;
        break;
      }
    }
  }

  // Gather result
  Reg_assignment result;
  result.reg_ids.resize(cc.num_variables);
  for (auto& vi: vinfos)
    result.reg_ids[vi.variable_id] = vi.assigned_reg;
  return result;
}

} // anon namespace

void emit_image(std::ostream& os, const Compiler_output& cc) {
  (void) os;
  auto assignment = assign_regs(cc);

  fmt::print("{} variables colored onto {} registers:\n", cc.num_variables, num_regs);
  for (int var_id = 0; var_id < cc.num_variables; var_id++) {
    int reg_id = assignment.reg_ids[var_id];
    fmt::print("  #{} ", var_id);
    if (reg_id == Reg_assignment::spilled)
      fmt::print("spilled\n");
    else
      fmt::print(" -> reg #{}\n", reg_id);

  }
}