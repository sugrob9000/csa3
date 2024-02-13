// The codegen pass takes the output of the "abstract" compiler
// and turns it into real code and real data

#pragma once
#include "compile.hpp"
#include <iosfwd>

void emit_image(std::ostream&, const Compiler_output&);