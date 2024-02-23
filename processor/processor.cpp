#include "processor.hpp"
#include "util.hpp"
#include <cassert>
#include <iostream>

namespace {

// MMIO inside memory manager is magic

constexpr u32 mmio_addr = 200;

void mmio_push(u32 c) {
  std::cout << char(c) << std::flush;
}

u32 mmio_get() {
  if (char c; std::cin >> c)
    return u32(c);
  else
    return 0;
}


enum class Opcode {
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

Processor::Alu::Op binop_to_alu(Opcode opcode) {
  switch (static_cast<Opcode>(opcode)) {
  case Opcode::add: return Processor::Alu::Op::add;
  case Opcode::sub: return Processor::Alu::Op::sub;
  case Opcode::mul: return Processor::Alu::Op::mul;
  case Opcode::div: return Processor::Alu::Op::div;
  case Opcode::mod: return Processor::Alu::Op::mod;
  case Opcode::cmp_equ: return Processor::Alu::Op::equ;
  case Opcode::cmp_lt: return Processor::Alu::Op::lt;
  case Opcode::cmp_gt: return Processor::Alu::Op::gt;
  default: return {}; // don't care
  }
}

} // anon namespace



Processor::Processor(std::span<const u32> image) {
  // Load memory image.
  // Doing it this way means that memory outside the image is not
  // really memory: it will ignore stores and loads will return a
  // constant value. But programs should never access that anyway
  // (hardware will, though)
  mem.memory.assign(image.begin(), image.end());

  { // Fiddle with processor state into beginning execution correctly
    // Will get incremented to 0 before fetching
    fetch.addr = -1;

    // Prime the pipeline with NOPs
    constexpr u32 encoded_nop = 0x3; // ad r0, r0, r0
    fetch.fetched_insn = encoded_nop;
    decoder_in = encoded_nop;
    mem.rdata = encoded_nop;
  }
}

void Processor::print_state() const {
  LOG("============= vvvvvvvvvvv =============");
  if (ctrl.stall)
    LOG("STALLED {}", ctrl.stall);
  LOG(
    "memw: {}, memr: {}, destw: {}, imm1: 0x{:x}, imm2: 0x{:x}",
    ctrl.mem_write, ctrl.mem_read,
    ctrl.dest_reg_write, ctrl.imm1, ctrl.imm2
  );
  {
    LOG_NOLN("non-0 regs:");
    for (int i = 0; i < 64; i++) {
      u32 r = reg.registers[i];
      if (r != 0)
        LOG_NOLN(" r{} = 0x{:X};", i, r);
    }
    LOG_NOLN("\n");
  }
  LOG("mem: addr 0x{:x}, wdata 0x{:x}, rdata 0x{:x}", mem.addr, mem.wdata, mem.rdata);
}

// ===========================================================================
// Simulating the processor

bool Processor::next_tick() {
  // What happens in this function is thought of as simultaneous, so
  // we need to carefully order the propagations to simulate the way
  // it "would have happened" in a real circuit

  get_ctrl_signals();
  print_state();

  if (ctrl.halt) {
    LOG("Halting");
    return false;
  }

  mem_perform();
  reg_readout();

  decoder_perform();

  fetch_perform();
  alu_perform();

  reg_writeback();
  mem_update_inputs();

  return true;
}

void Processor::get_ctrl_signals() {
  // Latest decoded signals become current control signals (control register latches)
  ctrl = next_ctrl;

  if (ctrl.stall > 0) {
    // If control unit is stalled, disable signals that would cause visible effects
    ctrl.mem_write = false;
    ctrl.dest_reg_write = false;
    ctrl.halt = false;
  }

  { // Verify state
    assert(ctrl.sel_src1_regid < 64);
    assert(ctrl.sel_src2_regid < 64);
    assert(ctrl.sel_dest_regid < 64);
    assert(!ctrl.mem_write || !ctrl.mem_read);
  }
}

void Processor::mem_perform() {
  if (ctrl.mem_write) {
    LOG("write {} <- 0x{:x}", mem.addr, mem.wdata);
    if (mem.addr == mmio_addr)
      mmio_push(mem.wdata);
    else if (mem.addr < mem.memory.size())
      mem.memory[mem.addr] = mem.wdata;
  }
  if (ctrl.mem_read) {
    if (mem.addr == mmio_addr)
      mem.rdata = mmio_get();
    else if (mem.addr < mem.memory.size())
      mem.rdata = mem.memory[mem.addr];
    else
      mem.rdata = 0xBADF00D;
    LOG("read {} -> 0x{:x}", mem.addr, mem.rdata);
  }
}

void Processor::reg_readout() {
  reg.src1 = reg.registers[ctrl.sel_src1_regid];
  reg.src2 = reg.registers[ctrl.sel_src2_regid];
}

void Processor::decoder_perform() {
  // Decoder decodes last tick's insn, creating next tick's control signals
  decoder_in = fetch.fetched_insn;
  next_ctrl = decode_insn(decoder_in);

  if (ctrl.stall) {
    assert(!ctrl.doing_jif);
    next_ctrl.stall = ctrl.stall - 1;
  }

  if (ctrl.doing_jif && reg.src1 != 0) {
    assert(!ctrl.stall);
    next_ctrl.stall = 2;
  }
}

void Processor::fetch_perform() {
  fetch.fetched_insn = mem.rdata;

  fetch.next_head_from_inc = fetch.addr + 1;
  fetch.next_head_from_jmp = ctrl.imm1;

  fetch.addr = [&] {
    if (ctrl.doing_jif) {
      if (reg.src1) {
        LOG("Fetch: passed jmp-if");
        return fetch.next_head_from_jmp;
      } else {
        LOG("Fetch: failed jmp-if");
        return fetch.next_head_from_inc;
      }
    } else {
      switch (ctrl.sel_fetch_head) {
        using enum Fetch::Head_mux;
      case from_inc:
        LOG("Fetch: +1");
        return fetch.next_head_from_inc;
      case from_jmp:
        LOG("Fetch: jmp");
        return fetch.next_head_from_jmp;
      case from_same:
        LOG("Fetch: stall, no change");
        return fetch.addr;
      }
      FATAL("Bad fetch mux");
    }
  }();
}

void Processor::alu_perform() {
  alu.op1_from_src1 = reg.src1;
  alu.op1_from_imm1 = ctrl.imm1;
  alu.op2_from_src2 = reg.src2;
  alu.op2_from_imm2 = ctrl.imm2;

  alu.src1 = ctrl.sel_alu_src1 == Alu::Src_mux::from_src_reg
    ? (alu.op1_from_src1)
    : (alu.op1_from_imm1);
  alu.src2 = ctrl.sel_alu_src2 == Alu::Src_mux::from_src_reg
    ? (alu.op2_from_src2)
    : (alu.op2_from_imm2);

  alu.result = [&] {
    switch (ctrl.sel_alu_op) {
    case Alu::Op::add: return alu.src1 + alu.src2;
    case Alu::Op::sub: return alu.src1 - alu.src2;
    case Alu::Op::mul: return alu.src1 * alu.src2;
    case Alu::Op::div: return alu.src1 / alu.src2;
    case Alu::Op::mod: return alu.src1 % alu.src2;
    case Alu::Op::equ: return (alu.src1 == alu.src2) ? 1u : 0u;
    case Alu::Op::lt: return (alu.src1 < alu.src2) ? 1u : 0u;
    case Alu::Op::gt: return (alu.src1 > alu.src2) ? 1u : 0u;
    }
    FATAL("Bad ALU op");
  } ();
}

void Processor::reg_writeback() {
  reg.dest_mux_from_mem = mem.rdata;
  reg.dest_mux_from_alu = alu.result;
  if (ctrl.dest_reg_write) {
    u32& dest = reg.registers[ctrl.sel_dest_regid];
    switch (ctrl.sel_reg_dest) {
    case Reg::Dest_mux::from_mem: dest = reg.dest_mux_from_mem; break;
    case Reg::Dest_mux::from_alu: dest = reg.dest_mux_from_alu; break;
    }
    LOG(
      "Writing to dest: r{} <- 0x{:x} (srcs 0x{:x}, 0x{:x})",
      ctrl.sel_dest_regid, dest,
      alu.src1, alu.src2
    );
  }
}

void Processor::mem_update_inputs() {
  mem.addr_mux_from_fetch = fetch.addr;
  mem.addr_mux_from_imm1 = ctrl.imm1;
  mem.addr_mux_from_src1 = reg.src1;

  mem.addr = [&] {
    switch (ctrl.sel_mem_addr) {
      case Mem::Addr_mux::from_fetch: return mem.addr_mux_from_fetch;
      case Mem::Addr_mux::from_imm1: return mem.addr_mux_from_imm1;
      case Mem::Addr_mux::from_src1: return mem.addr_mux_from_src1;
    }
    FATAL("Bad sel_mem_addr");
  }();

  mem.wdata = reg.dest;
}


auto Processor::decode_insn(u32 insn) -> Control_signals {
  // Decoder is magic
  Control_signals result {};
  LOG("Decoding 0x{:x}", insn);

  // All cycles except store's first cycle need us to load memory
  // (either for insn fetch or for load's first cycle)
  result.mem_read = true;

  // Unless this will be a memop, let insn fetch handle the memory op
  result.sel_mem_addr = Mem::Addr_mux::from_fetch;

  const auto opcode = static_cast<Opcode>(insn & 0xF);

  switch (opcode) {
  case Opcode::halt:
    result.halt = true;
    break;
  case Opcode::load:
  case Opcode::store:
    LOG("I don't like memops");
    break;
  case Opcode::jmp: {
    result.stall = 3;
    result.sel_fetch_head = Fetch::Head_mux::from_jmp;
    result.imm1 = insn >> 4;
    LOG("Decoded jmp to 0x{:x}", result.imm1);
    break;
  }
  case Opcode::jmp_if: {
    result.sel_src1_regid = (insn >> 4) & 0x3F;
    result.sel_fetch_head = Fetch::Head_mux::from_jmp;
    result.doing_jif = true;
    result.imm1 = insn >> 10;
    LOG("Decoded jif r{} to 0x{:x}", result.sel_src1_regid, result.imm1);
    break;
  }
  default: {
    // Binop
    result.sel_alu_op = binop_to_alu(opcode);
    const auto decode = [&] (u32 encoded, Alu::Src_mux& sel, u8& regid, u32& imm) {
      if (encoded & 1u) {
        sel = Alu::Src_mux::from_src_reg;
        regid = (encoded >> 1) & 0x3F;
      } else {
        sel = Alu::Src_mux::from_imm;
        imm = encoded >> 1;
      }
    };
    decode((insn >> 10) & 0x7FF, result.sel_alu_src1, result.sel_src1_regid, result.imm1);
    decode((insn >> 21) & 0x7FF, result.sel_alu_src2, result.sel_src2_regid, result.imm2);

    result.dest_reg_write = true;
    result.sel_reg_dest = Reg::Dest_mux::from_alu;

    LOG("Decoded binop");
    break;
  }
  }

  return result;
}

// ===========================================================================
// Simple processor model, for testing program logic

Simple_processor::Simple_processor(std::span<const u32> image):
  memory(image.begin(), image.end())
{ }

// NOLINTBEGIN cognitive complexity: this is throwaway code
bool Simple_processor::next_tick() {
  const u32 insn = memory[pc++];
  const auto opcode = static_cast<Opcode>(insn & 0xF);

  switch (opcode) {
  case Opcode::halt:
    return false;

  case Opcode::load:
  case Opcode::store: {
    const u8 reg_id = (insn >> 4) & 0x3F;
    u32 addr;
    if (insn & (1u << 10))
      addr = registers[(insn >> 11) & 0x3F];
    else
      addr = insn >> 11;
    if (opcode == Opcode::load) {
      if (addr == mmio_addr)
        registers[reg_id] = mmio_get();
      else if (addr < memory.size())
        registers[reg_id] = memory[addr];
      else
        registers[reg_id] = 0xBADF00D;
    } else {
      if (addr == mmio_addr)
        mmio_push(registers[reg_id]);
      else if (addr < memory.size())
        memory[addr] = registers[reg_id];
    }
    return true;
  }

  case Opcode::jmp:
    pc = insn >> 4;
    return true;

  case Opcode::jmp_if:
    if (registers[(insn >> 4) & 0x3F])
      pc = insn >> 10;
    return true;

  default: {
    // binop
    u32& dest = registers[(insn >> 4) & 0x3F];
    u32 src1, src2;

    if (insn & (1u << 10))
      src1 = registers[(insn >> 11) & 0x3F];
    else
      src1 = (insn >> 11) & 0x3FF;

    if (insn & (1u << 21))
      src2 = registers[(insn >> 22) & 0x3F];
    else
      src2 = (insn >> 22) & 0x3FF;

    switch (opcode) {
    case Opcode::add: dest = src1 + src2; break;
    case Opcode::sub: dest = src1 - src2; break;
    case Opcode::mul: dest = src1 * src2; break;
    case Opcode::div: dest = src1 / src2; break;
    case Opcode::mod: dest = src1 % src2; break;
    case Opcode::cmp_equ: dest = (src1 == src2) ? 1 : 0; break;
    case Opcode::cmp_gt: dest = (src1 > src2) ? 1 : 0; break;
    case Opcode::cmp_lt: dest = (src1 < src2) ? 1 : 0; break;
    default: FATAL("Bad opcode in insn 0x{:x} at pc 0x{:x}", insn, pc-1);
    }

    return true;
  }
  }
}
// NOLINTEND