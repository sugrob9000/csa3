#include "processor.hpp"
#include "util.hpp"
#include <cassert>
#include <fstream>
#include <span>
#include <vector>

#include <iostream>

static std::vector<std::byte> get_whole_file(const char* filename) {
  std::ifstream f(filename, std::ios::binary | std::ios::ate);
  if (!f)
    FATAL("Failed to load f '{}'", filename);

  const auto size = f.tellg();
  f.seekg(0, f.beg);

  std::vector<std::byte> image_mem(size);
  f.read(reinterpret_cast<char*>(image_mem.data()), size);
  return image_mem;
}

int main() {
  constexpr const char* image_filename = "memory-image";
  auto image_bytes = get_whole_file(image_filename);
  assert(image_bytes.size() % sizeof(u32) == 0);

  LOG("Loaded image '{}'", image_filename);

  Processor proc(std::span{
    reinterpret_cast<const u32*>(image_bytes.data()),
    image_bytes.size() / sizeof(u32)
  });

  while (proc.next_tick())
    ;
}