#include "diagnostics.hpp"
#include "stages.hpp"
#include <fstream>
#include <iostream>

int main() {
  auto ast = Ast::parse_stream(std::cin);
  auto ir = Ir::compile(ast);
  auto image = Hw_image::from_ir(std::move(ir));

  image.disasm();

  constexpr const char* out_filename = "image";
  std::ofstream out_stream(out_filename, std::ios::binary);
  if (!out_stream)
    error("Cannot open output file '{}'", out_filename);

  out_stream.write(
    reinterpret_cast<const char*>(image.words.data()),
    std::streamsize(sizeof(uint32_t) * image.words.size())
  );
}