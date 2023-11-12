#include <fmt/format.h>
#include <string_view>

int main() {
  using std::string_view;
  using fmt::print;
  using fmt::join;

  string_view qualifier = "sufficiently complicated";
  string_view languages[] = { "C", "Fortran" };
  string_view praise[] = { "ad-hoc", "informally-specified", "bug-ridden", "slow" };
  string_view carcinization_target = "half of Common Lisp";

  print("Any {} {} program contains an {} implementation of {}.\n",
      qualifier,
      join(languages, " or "),
      join(praise, ", "),
      carcinization_target);

  print("Hello from a prospective Lisp compiler!\n");
}