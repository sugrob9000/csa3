#pragma once
#include "insn.hpp"
#include <cstdint>
#include <fmt/format.h>
#include <span>

struct Memory_manager {
  // 32-bit arch addresses 4 GiB of main memory
  //constexpr static size_t memory_size_bytes = 0x1'0000'0000;
  constexpr static size_t memory_size_bytes = 0x0'0010'0000;

  enum class Action { none, read, write };

  // Owned mapping between host memory and emulated CPU memory
  std::span<uint32_t> memory;

  Action action = Action::none;
  uint32_t address;
  uint32_t value;

  Memory_manager();
  ~Memory_manager();
  Memory_manager(const Memory_manager&) = delete;
  auto operator=(const Memory_manager&) = delete;

  int latches_this_tick = 0;
  void latch();
};

struct Instruction_fetcher {
  uint32_t fetch_head;
  uint32_t out_instruction;
};

struct Decoder {
  uint32_t in_encoded = 0x9999'9998;
  Instruction out_decoded{};
};

struct Executor {
  Instruction in_instruction{};
  uint32_t instruction_pointer = 0;

  bool flags[8] = {};
  uint32_t registers[16] = {};

  void set_flag(int id, bool value);
  void set_register(int id, uint32_t value);
};

struct Processor {
  Memory_manager memory_manager{};

  Instruction_fetcher instruction_fetch{};
  Decoder decoder{};
  Executor executor{};

  bool halt_rq = false;

  Processor();
  void copy_image(std::span<const std::byte>);

  enum class Tick_result { stop, keep_going };
  Tick_result advance_tick();
};