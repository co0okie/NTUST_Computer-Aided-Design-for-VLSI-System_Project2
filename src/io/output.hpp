#pragma once
#include "core/tree.hpp"
namespace cbi {
std::string serialize_tree(Problem const &, Tree const &);
Expected<WriteSuccess> write_output(std::string const &, std::string const &);
} // namespace cbi
