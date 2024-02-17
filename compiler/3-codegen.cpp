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
      && op != IR_op::jump
      && op != IR_op::store;
}

bool has_src1(IR_op op) {
  return op != IR_op::halt;
}

bool has_src2(IR_op op) {
  return op != IR_op::halt
      && op != IR_op::mov
      && op != IR_op::load;
}

// ===========================================================================
// Register coloring.

struct Register_id { uint8_t id; };

// 2 registers are reserved for loads of spilled values and stores thereto.
// An instruction might have had both its source operands spilled, so we need 2.

constexpr int num_available_regs = 3;
//constexpr int num_available_regs = 62;

constexpr Register_id scratch_reg1 = { 62 };
constexpr Register_id scratch_reg2 = { 63 };

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
struct Memory_addr { uint32_t addr; }; // Will need patching after coloring
using Location = One_of<Memory_addr, Register_id>;

struct Coloring_result {
  std::vector<Location> locs;
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
    .locs = std::vector<Location>(lives.size()),
    .num_spilled_variables = 0
  };
  int32_t next_memory_addr = 0;

  for (int i = 0; i < lives.size(); i++) {
    int our_id = vars_by_life_length[i];
    auto& our_life = lives[our_id];
    bool taken[num_available_regs] = {};

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
    for (int reg_id = 0; reg_id < num_available_regs; reg_id++) {
      if (!taken[reg_id]) {
        result.locs[our_id] = Register_id(reg_id);
        found_register = true;
        break;
      }
    }

    if (!found_register) {
      result.locs[our_id] = Memory_addr(next_memory_addr++);
      result.num_spilled_variables++;
    }
  }

  return result;
}


// ===========================================================================
// Memory-aware codegen.

// Most of the same operations as the abstract `IR_op`, but
// without mov (`add X, 0` suffices) and with loads and stores

enum class Hw_op: uint8_t {
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
  jmp = 0xB,
  jmp_if = 0xC,
};

struct Immediate { uint32_t value; };
using Hw_value = One_of<Register_id, Immediate>;

struct Codegen {
  std::vector<uint32_t> static_data;
  std::vector<uint32_t> hw_code;
  Coloring_result coloring;

  // =========================================================================
  // Dealing with variables

  // Coloring gave us 0-based memory addresses for spilled variables,
  // but we might have other data, so relocate them on top of our static data.
  // This must be done BEFORE any codegen!
  void pre_fixup_spilled_variables() {
    size_t old_top = static_data.size();
    for (auto& loc: coloring.locs) {
      if (auto mem = loc.maybe_as<Memory_addr>())
        mem->addr += old_top;
    }
    static_data.resize(old_top + coloring.num_spilled_variables);
  }

  bool is_spilled(Variable_id var) { return coloring.locs[var.id].is<Memory_addr>(); }
  static bool is_large_for_binop(Constant c) { return c.value >= (1u << 10); }

  Memory_addr spill_constant(Constant c) {
    auto addr = uint32_t(static_data.size());
    static_data.push_back(c.value);
    return Memory_addr(addr);
  }

  // Can only be called with external knowledge that this is valid
  Register_id reg_of(Variable_id var) { return coloring.locs[var.id].as<Register_id>(); }
  Memory_addr addr_of(Variable_id var) { return coloring.locs[var.id].as<Memory_addr>(); }


  // =========================================================================
  // Mapping IR jumps to HW jumps
  //
  // The IR jump offsets cannot be used as-are, because:
  // 1. IR jumps base their offsets on the beginning of code, but we will put
  //    code after data, and we only know how much data there is after codegen
  // 2. Per an IR instruction, we may emit multiple or no HW instructions, so
  //    "IR offset -> HW offset" is not a linear relationship

  // Track a mapping from indices into the IR -> indices into HW code.
  // This is needed for correctly emitting backward jumps.
  std::vector<uint32_t> ir_to_hw_pos;
  std::vector<uint32_t> jumps_hw_pos; // The HW pos of all jumps

  // Patch all jumps to point to the correct places in HW
  // This must be called AFTER any codegen!
  void post_fixup_jumps() {
    // Code begins right after data
    auto code_offset = uint32_t(static_data.size());

    for (uint32_t jump_pos: jumps_hw_pos) {
      uint32_t& insn = hw_code[jump_pos];

      const auto opcode = static_cast<Hw_op>(insn & 0xF);
      assert(opcode == Hw_op::jmp || opcode == Hw_op::jmp_if);

      uint32_t imm_bit_pos = (opcode == Hw_op::jmp) ? 4 : 10;
      uint32_t ir_offset = insn >> imm_bit_pos;
      uint32_t hw_offset = ir_to_hw_pos[ir_offset] + code_offset;
      insn &= (1u << imm_bit_pos) - 1;
      insn |= hw_offset << imm_bit_pos;
    }

    // The program's entry point is at address 0, and there is a special
    // place at `static_data[0]` for us to create a jump to the real code
    static_data[0] = static_cast<uint32_t>(Hw_op::jmp) | (code_offset << 4);
  }

  // =========================================================================
  // Emitting HW instructions

  void emit_memop(Hw_op op, Register_id reg, Location addr) {
    assert(op == Hw_op::load || op == Hw_op::store);
    uint32_t high_bits = addr.match(
      [] (Register_id reg2) -> uint32_t { return reg2.id << 11; },
      [] (Memory_addr mem) -> uint32_t {
        constexpr unsigned width = 21;
        mem.addr &= (1u << width)-1;
        return (1u << 10) | (mem.addr << 11);
      }
    );
    hw_code.push_back(static_cast<uint32_t>(op) | (reg.id << 4) | high_bits);
  }

  void emit_binop(Hw_op op, Register_id dest, Hw_value src1, Hw_value src2) {
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
      static_cast<uint32_t>(op) |
      (dest.id << 4) |
      (convert_src(src1) << 10) |
      (convert_src(src2) << 21)
    );
  }

  void emit_jmp(uint32_t dest) {
    if (dest >= (1u << 28))
      error("Tried to jmp to absolute immediate address {} -- too large", dest);
    hw_code.push_back(static_cast<uint32_t>(Hw_op::jmp) | (dest << 4));
  }

  void emit_jmp_if(Register_id condition, uint32_t dest) {
    if (dest >= (1u << 22))
      error("Tried to jmp-if to absolute immediate address {} -- too large", dest);
    hw_code.push_back(
      static_cast<uint32_t>(Hw_op::jmp_if) |
      (condition.id << 4) |
      (dest << 10)
    );
  }

  // =========================================================================
  // Handling IR instructions

  // Put a constant into a register.
  // This may require a load if it does not fit into an immediate
  void handle_fetch_const(Register_id dest, Constant src) {
    if (is_large_for_binop(src))
      emit_memop(Hw_op::load, dest, spill_constant(src));
    else
      emit_binop(Hw_op::add, dest, Immediate(src.value), Immediate(0));
  }

  void handle_mov(Variable_id dest, Value src) {
    // --- Situation ---  ---- What do ----
    // 1.  reg <- reg     add R,0
    // 2.  reg <- mem     load
    // 3.  reg <- const   fetch_const (add I,0 or load)
    // 4.  mem <- reg     store
    // 5.  mem <- mem     load + store
    // 6.  mem <- const   fetch_const + store

    int dest_type = is_spilled(dest) ? 1 : 0;
    int src_type = src.match(
      [&] (Variable_id var) { return is_spilled(var) ? 1 : 0; },
      [] (Constant) { return 2; }
    );

    const auto src_reg = [&] { return reg_of(src.as<Variable_id>()); };
    const auto src_addr = [&] { return addr_of(src.as<Variable_id>()); };
    const auto src_const = [&] { return src.as<Constant>(); };

    // See table above
    int situation = 1 + src_type + 3 * dest_type;
    switch (situation) {
      using enum Hw_op;
    case 1: return emit_binop(add, reg_of(dest), src_reg(), Immediate(0));
    case 2: return emit_memop(load, reg_of(dest), src_addr());
    case 3: return handle_fetch_const(reg_of(dest), src_const());
    case 4: return emit_memop(store, src_reg(), addr_of(dest));
    case 5:
      emit_memop(load, scratch_reg1, src_reg());
      emit_memop(store, scratch_reg1, addr_of(dest));
      return;
    case 6:
      handle_fetch_const(scratch_reg1, src_const());
      emit_memop(store, scratch_reg1, addr_of(dest));
      return;
    default: unreachable();
    }
  }

  void handle_load(Variable_id dest, Value addr) {
    // ---- Situation ----  ---- What do ----
    // 1. reg <- mem[imm]   load imm
    // 2. reg <- mem[reg]   load reg
    // 3. reg <- mem[mem]   load imm + load reg
    // 4. mem <- mem[imm]   load imm + store imm
    // 5. mem <- mem[reg]   load reg + store imm
    // 6. mem <- mem[mem]   load imm + load reg + store imm

    // There is no provision for pointers which are too large,
    // those will just break codegen :(

    int dest_type = is_spilled(dest) ? 1 : 0;
    int src_type = addr.match(
      [] (Constant) { return 0; },
      [&] (Variable_id var) { return is_spilled(var) ? 2 : 1; }
    );

    const auto addr_imm = [&] { return Memory_addr(addr.as<Constant>().value); };
    const auto addr_reg = [&] { return reg_of(addr.as<Variable_id>()); };
    const auto addr_mem = [&] { return addr_of(addr.as<Variable_id>()); };

    // See table above
    int situation = 1 + src_type + 3 * dest_type;
    switch (situation) {
      using enum Hw_op;
    case 1: return emit_memop(load, reg_of(dest), addr_imm());
    case 2: return emit_memop(load, reg_of(dest), addr_reg());
    case 3:
      emit_memop(load, scratch_reg1, addr_mem());
      emit_memop(load, reg_of(dest), scratch_reg1);
      return;
    case 4:
      emit_memop(load, scratch_reg1, addr_imm());
      emit_memop(store, scratch_reg1, addr_of(dest));
      return;
    case 5:
      emit_memop(load, scratch_reg1, addr_reg());
      emit_memop(store, scratch_reg1, addr_of(dest));
      return;
    case 6:
      emit_memop(load, scratch_reg1, addr_mem());
      emit_memop(load, scratch_reg1, scratch_reg1);
      emit_memop(store, scratch_reg1, addr_of(dest));
      return;
    default: __builtin_unreachable();
    }
  }

  void handle_store(Value addr, Value src) {
    // Get the stored value into scratch_reg1 anyhow
    src.match(
      [&] (Constant c) { handle_fetch_const(scratch_reg1, c); },
      [&] (Variable_id var) {
        if (is_spilled(var))
          emit_memop(Hw_op::load, scratch_reg1, addr_of(var));
        else
          emit_binop(Hw_op::add, scratch_reg1, reg_of(var), Immediate(0));
      }
    );

    // For compiler complexity reasons, we never emit store-imm for an IR store...
    Register_id addr_reg = addr.match(
      [&] (Constant c) {
        handle_fetch_const(scratch_reg2, c);
        return scratch_reg2;
      },
      [&] (Variable_id var) {
        if (!is_spilled(var))
          return reg_of(var);
        emit_memop(Hw_op::load, scratch_reg2, addr_of(var));
        return scratch_reg2;
      }
    );

    // Perform the store
    emit_memop(Hw_op::store, scratch_reg1, addr_reg);
  }

  void handle_binop(IR_insn& insn) {
    Hw_op op = [&] {
      switch (insn.op) {
      case IR_op::add: return Hw_op::add;
      case IR_op::sub: return Hw_op::sub;
      case IR_op::mul: return Hw_op::mul;
      case IR_op::div: return Hw_op::div;
      case IR_op::mod: return Hw_op::mod;
      case IR_op::cmp_equ: return Hw_op::cmp_equ;
      case IR_op::cmp_gt: return Hw_op::cmp_gt;
      case IR_op::cmp_lt: return Hw_op::cmp_lt;
      default: __builtin_unreachable();
      }
    }();

    const auto convert_src = [&] (Register_id scratch, Value ir_src) {
      return ir_src.match(
        [&] (Variable_id var) -> Hw_value {
          if (!is_spilled(var))
            return reg_of(var);
          emit_memop(Hw_op::load, scratch, addr_of(var));
          return scratch;
        },
        [&] (Constant c) -> Hw_value {
          if (!is_large_for_binop(c))
            return Immediate(c.value);
          emit_memop(Hw_op::load, scratch, spill_constant(c));
          return scratch;
        }
      );
    };

    Hw_value src1 = convert_src(scratch_reg1, insn.src1);
    Hw_value src2 = convert_src(scratch_reg2, insn.src2);

    if (is_spilled(insn.dest)) {
      emit_binop(op, scratch_reg1, src1, src2);
      emit_memop(Hw_op::store, scratch_reg1, addr_of(insn.dest));
    } else {
      emit_binop(op, reg_of(insn.dest), src1, src2);
    }
  }

  void handle_jump(Value condition, Constant where) {
    jumps_hw_pos.push_back(hw_code.size());
    condition.match(
      [&] (Constant c) {
        if (c.value != 0)
          emit_jmp(where.value);
      },
      [&] (Variable_id var) {
        if (is_spilled(var)) {
          emit_memop(Hw_op::load, scratch_reg1, addr_of(var));
          emit_jmp_if(scratch_reg1, where.value);
        } else {
          emit_jmp_if(reg_of(var), where.value);
        }
      }
    );
  }

  void handle_ir_insn(IR_insn& insn) {
    ir_to_hw_pos.push_back(hw_code.size()); // Maintain the IR pos -> HW pos mapping

    switch (insn.op) {
      using enum IR_op;
    case halt: return hw_code.push_back(static_cast<uint32_t>(Hw_op::halt));
    case mov: return handle_mov(insn.dest, insn.src1);
    case jump: return handle_jump(insn.src1, insn.src2.as<Constant>());
    case load: return handle_load(insn.dest, insn.src1);
    case store: return handle_store(insn.src1, insn.src2);
    default: return handle_binop(insn);
    }
  }
};

} // anon namespace


void emit_image(std::ostream& os, IR_output&& ir) {
  Codegen codegen;

  auto code = std::move(ir.code);
  codegen.static_data = std::move(ir.data);

  {
    auto lifetimes = build_var_lifetimes(ir.num_variables, code);
    codegen.coloring = color_variables(lifetimes);
  }

  codegen.pre_fixup_spilled_variables();

  for (IR_insn& insn: code)
    codegen.handle_ir_insn(insn);

  codegen.post_fixup_jumps();

  disasm_hw(codegen.static_data, codegen.hw_code);
  (void) os;
}