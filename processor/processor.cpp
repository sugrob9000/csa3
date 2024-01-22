#include "processor.hpp"
#include "util.hpp"
#include <cassert>
#include <cstddef>
#include <exception>
#include <sys/mman.h>

Memory_manager::Memory_manager() {
  constexpr auto map_prot = PROT_READ | PROT_WRITE;
  constexpr auto map_flags = MAP_SHARED | MAP_ANONYMOUS;

  const auto map_result = ::mmap(nullptr, memory_size_bytes, map_prot, map_flags, -1, 0);
  if (map_result == MAP_FAILED)
    FATAL("memory: failed to map {} bytes", memory_size_bytes);

  LOG("memory: emulated range 0x{:08x}...0x{:08x} is host range {}...{}",
      0, memory_size_bytes,
      map_result,
      static_cast<void*>(static_cast<std::byte*>(map_result) + memory_size_bytes));

  const auto data_begin = reinterpret_cast<std::byte*>(map_result);
  const auto data_end = data_begin + memory_size_bytes;

  this->memory = {
    reinterpret_cast<uint32_t*>(data_begin),
    reinterpret_cast<uint32_t*>(data_end),
  };
}

Memory_manager::~Memory_manager() {
  ::munmap(memory.data(), memory_size_bytes);
}

void Memory_manager::latch() {
  switch (action) {
  case Action::read:
    value = memory[address];
    break;
  case Action::write:
    memory[address] = value;
    break;
  case Action::none:
    break;
  }
  latches_this_tick++;
}


void Executor::set_flag(int id, bool value) {
  if (id != 0)
    flags[id] = value;
}

void Executor::set_register(int id, uint32_t value) {
  if (id != 0)
    registers[id] = value;
}


Processor::Processor() {
  instruction_fetch.out_instruction = Instruction::encoded_nop();
  LOG("primed fetch output with {:08x}", instruction_fetch.out_instruction);

  decoder.out_decoded = Instruction::nop();
  LOG("primed decoder output with {}", decoder.out_decoded);

  // Due to pipelining, it will take two ticks before real instructions from the
  // image begin to execute, so pull the instruction pointer behind by two words
  executor.instruction_pointer = -8;
}

void Processor::copy_image(std::span<const std::byte> image) {
  auto image_size = image.size();
  const auto memory_size = sizeof(uint32_t) * memory_manager.memory.size();

  if (image_size > memory_size) {
    LOG("warning: image too large ({} > {}), truncating", image_size, memory_size);
    image_size = memory_size;
  }

  std::memcpy(memory_manager.memory.data(), image.data(), image_size);
}

auto Processor::advance_tick() -> Tick_result {
  bool should_log_regs = false;
  bool should_log_flags = false;

  { // Propagate state results down the pipeline
    decoder.in_encoded = instruction_fetch.out_instruction;
    executor.in_instruction = decoder.out_decoded;
  }

  { // Advance the state of each part of the pipeline
    memory_manager.latches_this_tick = 0;

    { // Instruction fetching
      memory_manager.action = Memory_manager::Action::read;
      memory_manager.address = instruction_fetch.fetch_head;
      memory_manager.latch();
      instruction_fetch.out_instruction = memory_manager.value;
      instruction_fetch.fetch_head++;
    }

    // Decoding
    decoder.out_decoded = decode(decoder.in_encoded);

    { // Execution
      executor.instruction_pointer += 4;
      auto& insn = executor.in_instruction;

      const auto get_src_value = [this] (Value_src src) {
        return src.type == Value_src::Type::imm
          ? src.id_or_value
          : executor.registers[src.id_or_value];
      };

      switch (insn.op) {
      case Operation_id::halt:
        halt_rq = true;
        break;
      case Operation_id::add: {
        const uint64_t a = get_src_value(insn.arith.src1);
        const uint64_t b = get_src_value(insn.arith.src2);
        const uint64_t result = a + b;
        const bool carry = !!(result >> 32);
        const uint32_t result_trunc = result;
        executor.set_register(insn.arith.dst1.id, result_trunc);
        executor.set_flag(insn.arith.dst_flag_id, carry);
        should_log_flags = true;
        should_log_regs = true;
        break;
      }
      default:
        break;
      }
    }
  }

  { // Logging
    constexpr std::string_view indent = "    ";
    LOG("(FETCH @ 0x{:08x} | 0x{:08x}) -> (DECODE 0x{:08x} | {}) -> (EXEC {} @ {}), {} mem",
        instruction_fetch.fetch_head, instruction_fetch.out_instruction,
        decoder.in_encoded, decoder.out_decoded,
        executor.in_instruction, executor.instruction_pointer,
        memory_manager.latches_this_tick);

    if (should_log_regs) {
      LOG_NOLN("{}", indent);
      for (int i = 0; i < 16; i++) {
        if (i == 8)
          LOG_NOLN("{}", indent);
        LOG_NOLN("r{} = 0x{:08x} ", i, executor.registers[i]);
        if (i % 8 == 7)
          LOG("");
      }
    }

    if (should_log_flags) {
      LOG_NOLN("{}f = ", indent);
      for (int i = 0; i < 8; i++)
        LOG_NOLN("{}", executor.flags[i] ? char('0' + i) : '-');
      LOG("");
    }
  }

  if (halt_rq) {
    LOG("Halting.");
    return Tick_result::stop;
  } else {
    return Tick_result::keep_going;
  }
}