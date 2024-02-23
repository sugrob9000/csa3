#pragma once
#include <cstdint>
#include <span>
#include <vector>

using u8 = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;

struct Processor {
  explicit Processor(std::span<const u32> image);
  bool next_tick(); // returns: whether halted
  void print_state() const;

  // =========================================================================
  // Processor state...

  struct Mem {
    std::vector<u32> memory;
    u32 addr;
    u32 wdata;
    u32 rdata;

    enum class Addr_mux: u8 { from_fetch, from_imm1, from_src1 };
    u32 addr_mux_from_fetch;
    u32 addr_mux_from_imm1;
    u32 addr_mux_from_src1;
  } mem = {};

  struct Reg {
    u32 registers[64];
    u32 src1;
    u32 src2;
    u32 dest;

    enum class Dest_mux: u8 { from_mem, from_alu };
    u32 dest_mux_from_mem;
    u32 dest_mux_from_alu;
  } reg = {};

  struct Alu {
    enum class Op: u8 { add, sub, mul, div, mod, equ, lt, gt };

    enum class Src_mux: u8 { from_src_reg, from_imm };
    u32 op1_from_src1;
    u32 op1_from_imm1;
    u32 op2_from_src2;
    u32 op2_from_imm2;

    u32 src1;
    u32 src2;
    u32 result;
  } alu = {};

  struct Fetch {
    enum class Head_mux: u8 { from_inc, from_jmp, from_same };

    u32 next_head_from_inc;
    u32 next_head_from_jmp;

    u32 fetched_insn;
    u32 addr;
  } fetch = {};

  u32 decoder_in = 0; // Instruction to decode

  // Decoder output
  struct Control_signals {
    bool halt; // Not really a wire in the processor, but we use it to stop simulation
    u8 stall;

    bool mem_write;
    bool mem_read;
    Mem::Addr_mux sel_mem_addr;

    Reg::Dest_mux sel_reg_dest;
    bool dest_reg_write;
    u8 sel_dest_regid;
    u8 sel_src1_regid;
    u8 sel_src2_regid;

    Alu::Op sel_alu_op;
    Alu::Src_mux sel_alu_src1;
    Alu::Src_mux sel_alu_src2;

    Fetch::Head_mux sel_fetch_head;
    bool doing_jif;

    u32 imm1;
    u32 imm2;
  };
  Control_signals next_ctrl = {};
  Control_signals ctrl = {};

  static Control_signals decode_insn(u32);
  void mem_perform();
  void reg_readout();

  void decoder_perform();

  void fetch_perform();
  void alu_perform();

  void reg_writeback();
  void mem_update_inputs();
};


// ===========================================================================
// Simpler "magic" processor with same interface, for testing program logic

struct Simple_processor {
  explicit Simple_processor(std::span<const u32> image);
  bool next_tick();
  std::vector<u32> memory;
  u32 registers[64] = {};
  u32 pc = 0;
};