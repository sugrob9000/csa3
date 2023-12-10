#include "processor.hpp"
#include "util.hpp"
#include <fstream>
#include <vector>

static std::vector<std::byte> get_whole_file(const char* filename) {
  std::ifstream f(filename, std::ios::binary);
  if (!f)
    FATAL("Failed to load f '{}'", filename);

  f.seekg(0, f.end);
  const auto size = f.tellg();
  f.seekg(0, f.beg);

  std::vector<std::byte> image_mem(size);
  f.read(reinterpret_cast<char*>(image_mem.data()), size);
  return image_mem;
}

int main() {
  Processor processor;

  constexpr const char* image_filename = "memory-image";
  processor.copy_image(get_whole_file(image_filename));
  LOG("Loaded image '{}'", image_filename);

  LOG("Starting execution.");
  for (int i = 0; i < 100; i++) {
    auto result = processor.advance_tick();
    if (result == Processor::Tick_result::stop)
      break;
  }
}