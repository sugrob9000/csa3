#pragma once
#include "util.hpp"
#include <cstdint>
#include <iosfwd>
#include <string>
#include <vector>

// More of an Abstract Syntax Forest, strictly speaking.
struct Ast {
  struct Node; // fwd declare for std::vector in Parens
  struct Identifier { std::string name; };
  struct Number { int32_t value; };
  struct String { std::string value; };
  struct Parens { std::vector<Node> children; };
  struct Node: util::Variant<Identifier, Number, String, Parens> {};

  std::vector<Parens> toplevel_exprs;

  static Ast parse_stream(std::istream&);
};