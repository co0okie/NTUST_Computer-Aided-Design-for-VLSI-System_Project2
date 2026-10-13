#pragma once
#include "core/tree.hpp"

namespace cbi {
struct Topology {
    std::vector<NodeId> parents;
    std::vector<NodeId> order;
};
[[nodiscard]] std::optional<Integer> checked_add(Integer a, Integer b);
[[nodiscard]] std::optional<Integer> checked_distance(Position a, Position b);
[[nodiscard]] Expected<Topology> check_topology(Problem const& problem, Tree const& tree);
[[nodiscard]] Expected<Metrics> evaluate_tree(Problem const& problem, Tree const& tree);
[[nodiscard]] std::optional<BufferTypeId> feasible_type(Problem const& problem, std::size_t fanout, Integer length);
[[nodiscard]] Expected<WriteSuccess> normalize_types(Problem const& problem, Tree& tree);
}
