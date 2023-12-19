#pragma once
#include "util.hpp"
#include <cstdint>
#include <iosfwd>
#include <string>
#include <vector>

namespace ast {

struct Node;

// A bound (perhaps global) variable
struct Node_binding { std::string name; };

// A numeric constant
struct Node_constant { int32_t value; };

// A function call, i.e. anything of the form (a b c)
struct Node_call { std::vector<Node> children; };

using Any_node = util::Variant<Node_binding, Node_constant, Node_call>;

struct Node: private Any_node {
  using Any_node::Any_node, Any_node::is, Any_node::as;
};

struct Tree {
  std::vector<Node_call> root_expressions;
  static Tree from_text(std::istream&);
};

} // namespace ast