#include <fmt/color.h>
#include <fmt/core.h>

int main() {
  fmt::print("Determining whether the input program necessarily halts...\n");
  fmt::print(fg(fmt::color::red),
      "Couldn't prove it. Refusing to run it just in case.\n");
  return 1;
}