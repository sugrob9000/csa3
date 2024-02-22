#include "processor.hpp"
#include "util.hpp"
#include <cassert>
#include <iostream>

namespace {

void mmio_push(u32 c) {
  assert(c < 128);
  std::cout << char(c);
}

u32 mmio_get() {
  LOG("Reading MMIO...");
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

Processor::Control_signals decode_insn(u32 insn) {
  Processor::Control_signals result{};

  u8 opcode = insn & 0xF;

  switch (static_cast<Opcode>(opcode)) {
  case Opcode::halt:
    result.halt = true;
    break;
  case Opcode::jmp:
  case Opcode::jmp_if:
    FATAL("I don't like jumps");
    break;
  default:
    LOG("This is 0x{:x}", insn);
    break;
  }

  return result;
}

} // anon namespace

Processor::Processor(std::span<const u32> image) {
  // Load memory image.
  // Doing it this way means that memory outside the image is not
  // really memory: it will ignore stores and loads will return a
  // constant value. But programs should never access that anyway
  // (hardware will, though)
  mem.memory.assign(image.begin(), image.end());

  { // Iniitalize processor state so it begins execution
    fetch.addr = -1;

    // Prime the pipeline with NOPs
    fetch.fetched_insn = 0x3; // add r0, r0, r0
  }
}

bool Processor::next_tick() {
  // What happens in this function is thought of as simultaneous, so
  // we need to carefully order the propagations to simulate the way
  // it "would have happened" in a real circuit

  // Last tick's decoded signals become this tick's control signals
  ctrl = next_ctrl;

  if (ctrl.halt) {
    LOG("Halting");
    return false;
  }

  { // A memory operation happens, if any
    assert(!ctrl.mem_write || !ctrl.mem_read);

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

  { // Decoder decodes last tick's insn, creating next tick's control signals
    decoder_in = fetch.fetched_insn;
    next_ctrl = decode_insn(decoder_in);
  }

  { // Instruction fetch proceeds
    fetch.fetched_insn = mem.rdata;
    fetch.next_head_from_inc = fetch.addr + 1;
    fetch.next_head_from_imm = ctrl.imm1;

    switch (ctrl.fetch_next_head_mux) {
      case Fetch::Next_head_mux::from_inc: fetch.addr = fetch.next_head_from_inc; break;
      case Fetch::Next_head_mux::from_imm: fetch.addr = fetch.next_head_from_imm; break;
    }

    // unless (TODO) stalled, tell memory to read the next insn
    if (true) {
      next_ctrl.mem_read = true;
      next_ctrl.sel_mem_addr = Mem::Addr_mux::from_fetch;
    }
  }

  { // Regfile operations
    assert(ctrl.sel_src1_regid < 64);
    assert(ctrl.sel_src2_regid < 64);
    assert(ctrl.sel_dest_regid < 64);

    reg.dest_mux_from_mem = mem.rdata;
    reg.dest_mux_from_alu = alu.result;

    reg.src1 = reg.registers[ctrl.sel_src1_regid];
    reg.src2 = reg.registers[ctrl.sel_src2_regid];

    if (ctrl.dest_reg_write) {
      u32& dest = reg.registers[ctrl.sel_dest_regid];
      switch (ctrl.sel_reg_dest) {
        case Reg::Dest_mux::from_mem: dest = reg.dest_mux_from_mem; break;
        case Reg::Dest_mux::from_alu: dest = reg.dest_mux_from_alu; break;
      }
    }
  }

  { // Feed values into memory manager
    mem.addr_mux_from_fetch = fetch.addr;
    mem.addr_mux_from_imm1 = ctrl.imm1;
    mem.addr_mux_from_src1 = reg.src1;

    switch (ctrl.sel_mem_addr) {
      case Mem::Addr_mux::from_fetch: mem.addr = mem.addr_mux_from_fetch; break;
      case Mem::Addr_mux::from_imm1: mem.addr = mem.addr_mux_from_imm1; break;
      case Mem::Addr_mux::from_src1: mem.addr = mem.addr_mux_from_src1; break;
    }

    mem.wdata = reg.dest;
  }

  return true;
}