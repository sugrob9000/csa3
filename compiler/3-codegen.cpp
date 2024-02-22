#include "diagnostics.hpp"
#include "stages.hpp"
#include <algorithm>
#include <fmt/core.h>
#include <fmt/format.h>
#include <numeric>
#include <span>
#include <utility>

namespace {

// Used as general-purpose tags (in sum types) throughout the codegen pass
struct Register { uint8_t id; };
struct Immediate { uint32_t value; };
struct Address { uint32_t addr; };

// ===========================================================================
// Register coloring.

constexpr int num_gp_registers = 62;

// A variable is considered alive between its first and last usage, inclusive
struct Lifetime {
  int start = std::numeric_limits<int>::max();
  int end = std::numeric_limits<int>::min();
};

auto build_var_lifetimes(int num_variables, std::span<Ir::Insn> code) {
	std::vector<Lifetime> result(num_variables);

  for (int insn_id = 0; insn_id < code.size(); insn_id++) {
    const auto maybe_update_lifetime = [&] (Ir::Value value) {
      if (auto var = value.maybe_as<Ir::Variable>()) {
				auto& life = result[var->id];
				life.start = std::min(life.start, insn_id);
				life.end = std::max(life.end, insn_id);
      }
    };
    auto& insn = code[insn_id];
    if (insn.has_valid_dest()) maybe_update_lifetime(insn.dest);
    if (insn.has_valid_src1()) maybe_update_lifetime(insn.src1);
    if (insn.has_valid_src2()) maybe_update_lifetime(insn.src2);
  }

	return result;
}

// The places a variable can be assigned to live in
using Location = One_of<Address, Register>;

struct Coloring_result {
  std::vector<Location> locs;
  int num_spilled_variables;
};

Coloring_result color_variables
(std::span<Lifetime> lives, uint32_t mem_base, int num_avail_regs) {
  // Sort variables by ascending length of life, putting "hot" ones first
  std::vector<int> vars_by_life_length(lives.size());
  std::iota(vars_by_life_length.begin(), vars_by_life_length.end(), 0);
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
  uint32_t next_memory_addr = mem_base;

  for (int i = 0; i < lives.size(); i++) {
    int our_id = vars_by_life_length[i];
    auto& our_life = lives[our_id];

    bool taken_storage[num_gp_registers] = {};
    auto taken = std::span{ taken_storage, taken_storage + num_avail_regs };

    for (int j = 0; j < i; j++) {
      int their_id = vars_by_life_length[j];
      auto& their_life = lives[their_id];
      auto their_reg = result.locs[their_id].maybe_as<Register>();
      if (their_reg
      && our_life.end >= their_life.start
      && our_life.start <= their_life.end)
        taken[their_reg->id] = true;
    }

    auto it = std::find(taken.begin(), taken.end(), false);
    if (it != taken.end()) {
      result.locs[our_id] = Register(it - taken.begin());
    } else {
      result.locs[our_id] = Address(next_memory_addr++);
      result.num_spilled_variables++;
    }
  }

  return result;
}


// ===========================================================================
// Memory-aware codegen.
// The ISA has mostly the same operations as `Ir::Op`, but with differences
// that codegen must reconcile.

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

// 2 registers are reserved for loads of spilled values and stores thereto.
// An instruction might have had both its source operands spilled, so we need 2.
constexpr Register scratch_reg1 = { 62 };
constexpr Register scratch_reg2 = { 63 };

struct Codegen {
  std::vector<uint32_t> static_data;
  std::vector<uint32_t> hw_code;
  std::vector<Location> var_locs;

  // =========================================================================
  // Dealing with variables

  // Admit a coloring produced by `color_variables()`.
  // Must be called once BEFORE any codegen!
  void use_coloring(Coloring_result&& coloring) {
    var_locs = std::move(coloring.locs);
    // Coloring gave us correct addresses for variable homes,
    // but we still need to allocate space for them
    static_data.resize(static_data.size() + coloring.num_spilled_variables);
  }

  bool is_spilled(Ir::Variable var) { return var_locs[var.id].is<Address>(); }
  static bool is_large_for_binop(Ir::Constant c) { return c.value >= (1u << 10); }

  Address spill_constant(Ir::Constant c) {
    auto addr = uint32_t(static_data.size());
    static_data.push_back(c.value);
    return Address(addr);
  }

  // Can only be called with external knowledge that this is valid
  Register reg_of(Ir::Variable var) { return var_locs[var.id].as<Register>(); }
  Address addr_of(Ir::Variable var) { return var_locs[var.id].as<Address>(); }


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
  void remember_jump() {
    jumps_hw_pos.push_back(hw_code.size());
  }

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

  // Abuses the meaning of `Location`, but it has the tags we need
  void emit_memop(Hw_op op, Register reg, Location addr) {
    assert(op == Hw_op::load || op == Hw_op::store);
    uint32_t high_bits = addr.match(
      [] (Register reg2) -> uint32_t {
        return (1u << 10) | reg2.id << 11;
      },
      [] (Address mem) -> uint32_t {
        if (mem.addr >= (1u << 21))
          error("Absolute immediate address {:x} is too high for memop", mem.addr);
        return mem.addr << 11;
      }
    );
    hw_code.push_back(static_cast<uint32_t>(op) | (reg.id << 4) | high_bits);
  }

  using Binop_src = One_of<Register, Immediate>;
  void emit_binop(Hw_op op, Register dest, Binop_src src1, Binop_src src2) {
    const auto convert_src = [] (Binop_src src) -> uint32_t {
      return src.match(
        [] (Register reg) { return 1u | (reg.id << 1); },
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
      error("Absolute immediate address {:x} is too high for jmp", dest);
    remember_jump();
    hw_code.push_back(static_cast<uint32_t>(Hw_op::jmp) | (dest << 4));
  }

  void emit_jmp_if(Register condition, uint32_t dest) {
    if (dest >= (1u << 22))
      error("Absolute immediate address {:x} is too high for jmp-if", dest);
    remember_jump();
    hw_code.push_back(
      static_cast<uint32_t>(Hw_op::jmp_if) |
      (condition.id << 4) |
      (dest << 10)
    );
  }

  // =========================================================================
  // Handling higher-level IR instructions to emit low-level HW instructions.
  // Note that an IR instruction may correspond to zero, one, or more HW instructions

  // Put a constant into a register.
  // This may require a load if it does not fit into an immediate
  void handle_fetch_const(Register dest, Ir::Constant src) {
    if (is_large_for_binop(src))
      emit_memop(Hw_op::load, dest, spill_constant(src));
    else
      emit_binop(Hw_op::add, dest, Immediate(src.value), Immediate(0));
  }

  void handle_mov(Ir::Variable dest, Ir::Value src) {
    // --- Situation ---  ---- What do ----
    // 1.  reg <- reg     add R,0
    // 2.  reg <- mem     load
    // 3.  reg <- const   fetch_const
    // 4.  mem <- reg     store
    // 5.  mem <- mem     load + store
    // 6.  mem <- const   fetch_const + store

    int dest_type = is_spilled(dest) ? 1 : 0;
    int src_type = src.match(
      [&] (Ir::Variable var) { return is_spilled(var) ? 1 : 0; },
      [] (Ir::Constant) { return 2; }
    );

    const auto src_reg = [&] { return reg_of(src.as<Ir::Variable>()); };
    const auto src_addr = [&] { return addr_of(src.as<Ir::Variable>()); };
    const auto src_const = [&] { return src.as<Ir::Constant>(); };

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

  void handle_load(Ir::Variable dest, Ir::Value addr) {
    // ---- Situation ----  ---- What do ----
    // 1. reg <- mem[imm]   load imm
    // 2. reg <- mem[reg]   load reg
    // 3. reg <- mem[mem]   load imm + load reg
    // 4. mem <- mem[imm]   load imm + store imm
    // 5. mem <- mem[reg]   load reg + store imm
    // 6. mem <- mem[mem]   load imm + load reg + store imm

    // There is no provision for pointers which are too large,
    // those will just cause broken codegen :(

    int dest_type = is_spilled(dest) ? 1 : 0;
    int src_type = addr.match(
      [] (Ir::Constant) { return 0; },
      [&] (Ir::Variable var) { return is_spilled(var) ? 2 : 1; }
    );

    const auto addr_imm = [&] { return Address(addr.as<Ir::Constant>().value); };
    const auto addr_reg = [&] { return reg_of(addr.as<Ir::Variable>()); };
    const auto addr_mem = [&] { return addr_of(addr.as<Ir::Variable>()); };

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
    default: unreachable();
    }
  }

  void handle_store(Ir::Value addr, Ir::Value src) {
    // To reduce compiler complexity, we never emit store-imm for an IR store,
    // even if addr is a small constant...

    // Get the stored value into scratch_reg1 anyhow
    src.match(
      [&] (Ir::Constant c) { handle_fetch_const(scratch_reg1, c); },
      [&] (Ir::Variable var) {
        if (is_spilled(var))
          emit_memop(Hw_op::load, scratch_reg1, addr_of(var));
        else
          emit_binop(Hw_op::add, scratch_reg1, reg_of(var), Immediate(0));
      }
    );

    Register addr_reg = addr.match(
      [&] (Ir::Constant c) {
        handle_fetch_const(scratch_reg2, c);
        return scratch_reg2;
      },
      [&] (Ir::Variable var) {
        if (!is_spilled(var))
          return reg_of(var);
        emit_memop(Hw_op::load, scratch_reg2, addr_of(var));
        return scratch_reg2;
      }
    );

    // Perform the store
    emit_memop(Hw_op::store, scratch_reg1, addr_reg);
  }

  void handle_binop(Ir::Insn& insn) {
    Hw_op op = [&] {
      switch (insn.op) {
      case Ir::Op::add: return Hw_op::add;
      case Ir::Op::sub: return Hw_op::sub;
      case Ir::Op::mul: return Hw_op::mul;
      case Ir::Op::div: return Hw_op::div;
      case Ir::Op::mod: return Hw_op::mod;
      case Ir::Op::cmp_equ: return Hw_op::cmp_equ;
      case Ir::Op::cmp_gt: return Hw_op::cmp_gt;
      case Ir::Op::cmp_lt: return Hw_op::cmp_lt;
      default: unreachable();
      }
    }();

    const auto convert_src = [&] (Register scratch, Ir::Value ir_src) {
      return ir_src.match(
        [&] (Ir::Variable var) -> Binop_src {
          if (!is_spilled(var))
            return reg_of(var);
          emit_memop(Hw_op::load, scratch, addr_of(var));
          return scratch;
        },
        [&] (Ir::Constant c) -> Binop_src {
          if (!is_large_for_binop(c))
            return Immediate(c.value);
          emit_memop(Hw_op::load, scratch, spill_constant(c));
          return scratch;
        }
      );
    };

    Binop_src src1 = convert_src(scratch_reg1, insn.src1);
    Binop_src src2 = convert_src(scratch_reg2, insn.src2);

    if (is_spilled(insn.dest)) {
      emit_binop(op, scratch_reg1, src1, src2);
      emit_memop(Hw_op::store, scratch_reg1, addr_of(insn.dest));
    } else {
      emit_binop(op, reg_of(insn.dest), src1, src2);
    }
  }

  void handle_jump(Ir::Value condition, Address where) {
    condition.match(
      [&] (Ir::Constant c) {
        if (c.value != 0)
          emit_jmp(where.addr);
      },
      [&] (Ir::Variable var) {
        if (is_spilled(var)) {
          emit_memop(Hw_op::load, scratch_reg1, addr_of(var));
          emit_jmp_if(scratch_reg1, where.addr);
        } else {
          emit_jmp_if(reg_of(var), where.addr);
        }
      }
    );
  }

  void handle_ir_insn(Ir::Insn& insn) {
    ir_to_hw_pos.push_back(hw_code.size()); // Maintain the IR pos -> HW pos mapping
    switch (insn.op) {
      using enum Ir::Op;
    case halt: return hw_code.push_back(static_cast<uint32_t>(Hw_op::halt));
    case mov: return handle_mov(insn.dest, insn.src1);
    case jump: return handle_jump(insn.src1, Address(insn.src2.as<Ir::Constant>().value));
    case load: return handle_load(insn.dest, insn.src1);
    case store: return handle_store(insn.src1, insn.src2);
    default: return handle_binop(insn);
    }
  }
};

} // anon namespace


Hw_image Hw_image::from_ir(Ir&& ir) {
  Codegen codegen;

  auto code = std::move(ir.code);
  codegen.static_data = std::move(ir.data);

  {
    // This can be between 1 and `num_gp_registers`. There is no reason for it to
    // be fewer than `num_gp_registers` other than to test spilling behavior
    constexpr int registers_used = num_gp_registers;
    auto lifetimes = build_var_lifetimes(ir.num_variables, code);
    codegen.use_coloring(color_variables(
      lifetimes,
      codegen.static_data.size(),
      registers_used
    ));
  }

  // Perform code generation
  for (Ir::Insn& insn: code)
    codegen.handle_ir_insn(insn);
  codegen.post_fixup_jumps();

  // Gather result
  Hw_image result;
  result.data_break = codegen.static_data.size();
  result.words = std::move(codegen.static_data);
  result.words.insert(result.words.end(), codegen.hw_code.begin(), codegen.hw_code.end());
  return result;
}

// ===========================================================================
// Disassembly for debugging

struct Imm_or_reg { uint32_t encoded; };

template<> struct fmt::formatter<Imm_or_reg> {
  constexpr auto parse(fmt::format_parse_context& ctx) { return ctx.begin(); }
  appender format(Imm_or_reg src, format_context& ctx) {
    if (src.encoded & 1)
      return format_to(ctx.out(), "r{}", src.encoded >> 1);
    else
      return format_to(ctx.out(), "0x{:x}", src.encoded >> 1);
  }
};

struct Disassembler {
  int current_addr;
  std::span<const uint32_t> words;

  constexpr static std::string_view insn_names[] = {
    "halt",
    "ld", "st",
    "add", "sub", "mul", "div", "mod",
    "equ", "gt ", "lt ",
    "jmp", "jif",
  };

  void disasm_raw(std::string_view tag) {
    fmt::print("{:3x}: ({}) 0x{:x}\n", current_addr, tag, words[current_addr]);
    current_addr++;
  }

  void disasm_insn() {
    uint32_t insn = words[current_addr];
    uint32_t opcode = insn & 0xF;

    const auto fmt_operands = [&] () -> std::string {
      switch (static_cast<Hw_op>(opcode)) {
        using enum Hw_op;
      case halt: return fmt::format("0x{:x}", insn >> 4);
      case load:
      case store:
        return fmt::format(
          "r{}, mem[{}]",
          (insn >> 4) & 0x3F,
          Imm_or_reg(insn >> 10)
        );
      case jmp: return fmt::format("0x{:x}", insn >> 4);
      case jmp_if: return fmt::format("r{}, 0x{:x}", (insn >> 4) & 0x3F, insn >> 10);
      default:
        return fmt::format(
          "r{}, {}, {}",
          (insn >> 4) & 0x3F,
          Imm_or_reg((insn >> 10) & 0x7FF),
          Imm_or_reg(insn >> 21)
        );
      }
    };

    fmt::print("{:3x}: ", current_addr);
    if (opcode < 0xD)
      fmt::print("{} {}\n", insn_names[opcode], fmt_operands());
    else
      fmt::print("??? 0x{:08x}\n", insn);
    current_addr++;
  }
};

void Hw_image::disasm() const {
  Disassembler dis = {
    .current_addr = 0,
    .words = words
  };

  dis.disasm_insn(); // first jmp
  dis.disasm_raw("mmio");

  while (dis.current_addr < data_break)
    dis.disasm_raw("data");

  while (dis.current_addr < dis.words.size())
    dis.disasm_insn();
}