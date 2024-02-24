#include "diagnostics.hpp"
#include "stages.hpp"
#include <fstream>

int main(int argc, char** argv) {
  if (argc != 3)
    error("Usage: {} <victim.lisp> <output-image>", argv[0]);

#if 1
  const char* in_filename = argv[1];
  std::ifstream input(in_filename);
  if (!input)
    error("Cannot open victim '{}'", in_filename);

  auto ast = Ast::parse_stream(input);
  input.close();

  auto ir = Ir::compile(ast);
  auto image = Hw_image::from_ir(std::move(ir));
#else
  auto image = Hw_image{
    .words = {
      0x3 | (0x20 << 11) | (0x1 << 21),
      0x1 | (0x01 << 4),
      0x3 | (0x20 << 11) | (0x1 << 21),
      0x2 | (0x01 << 4),
      0x3 | (0x20 << 11) | (0x1 << 21),
      0x1 | (0x01 << 4),
      0x3 | (0x20 << 11) | (0x1 << 21),
      0x2 | (0x01 << 4),
      0xB,
      0,
    },
    .data_break = 0,
  };
#endif

  const char* out_filename = argv[2];
  std::ofstream out_stream(out_filename, std::ios::binary);
  if (!out_stream)
    error("Cannot open output file '{}'", out_filename);

  out_stream.write(
    reinterpret_cast<const char*>(image.words.data()),
    std::streamsize(sizeof(uint32_t) * image.words.size())
  );
}