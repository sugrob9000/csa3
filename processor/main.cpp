#include "processor.hpp"
#include "util.hpp"
#include <cassert>
#include <fstream>
#include <span>
#include <vector>

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

int main(int argc, char** argv) {
  if (argc != 2)
    FATAL("Usage: {} <image>", argv[0]);

  const char* image_filename = argv[1];
  auto image_bytes = get_whole_file(image_filename);
  assert(image_bytes.size() % sizeof(u32) == 0);

  auto image_u32s = std::span{
    reinterpret_cast<const u32*>(image_bytes.data()),
    image_bytes.size() / sizeof(u32)
  };

  Processor proc(image_u32s);

  while (proc.next_tick())
    ;

  LOG("Ticked: {}, stalled: {}", proc.stats.ticked, proc.stats.stalled);
}