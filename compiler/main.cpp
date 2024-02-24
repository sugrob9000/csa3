#include "diagnostics.hpp"
#include "stages.hpp"
#include <fstream>

int main(int argc, char** argv) {
  if (argc != 3)
    error("Usage: {} <victim.lisp> <output-image>", argv[0]);

  const char* in_filename = argv[1];
  std::ifstream input(in_filename);
  if (!input)
    error("Cannot open victim '{}'", in_filename);

  auto ast = Ast::parse_stream(input);
  input.close();

  auto ir = Ir::compile(ast);
  auto image = Hw_image::from_ir(std::move(ir));

  const char* out_filename = argv[2];
  std::ofstream out_stream(out_filename, std::ios::binary);
  if (!out_stream)
    error("Cannot open output file '{}'", out_filename);

  out_stream.write(
    reinterpret_cast<const char*>(image.words.data()),
    std::streamsize(sizeof(uint32_t) * image.words.size())
  );
}