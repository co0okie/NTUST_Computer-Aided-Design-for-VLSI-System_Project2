#pragma once
#include "model.hpp"
#include <optional>
#include <limits>
#include <stdexcept>
namespace cbi {
using Integer = std::int64_t;
inline constexpr std::size_t component_budget = 100000;
inline constexpr Integer chain_distance_budget = 4096;
inline constexpr std::size_t chain_transition_budget = 262144;
struct Node {
    Position position;
    int type; // -1: source, -2: sink; otherwise buffer library index
    std::vector<std::size_t> children;
    Integer low = 0;
    Integer high = 0;
};
using Tree = std::vector<Node>;

Integer add(Integer a, Integer b);
Integer distance(Position a, Position b);
Tree initial_tree(Problem const& p, bool parity);
Metrics validate_tree(Problem const& p, Tree const& tree);
int cheapest_buffer_type(Problem const& p, std::size_t fanout, Integer length);
void select_buffer_types(Problem const& p, Tree& tree);
}
