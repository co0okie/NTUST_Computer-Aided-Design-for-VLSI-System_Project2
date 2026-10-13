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
enum class NodeKind { source, sink, buffer };
struct BufferTypeId {
    std::size_t value;
};
struct NodeId {
    std::size_t value;
};
struct Node {
    Position position;
    NodeKind kind;
    BufferTypeId buffer_type{0};
    std::vector<std::size_t> children;
    Integer low = 0;
    Integer high = 0;

    Node(Position point, NodeKind role, std::vector<std::size_t> loads,
         Integer minimum = 0, Integer maximum = 0)
        : position(point), kind(role), children(std::move(loads)), low(minimum), high(maximum) {}
    Node(Position point, BufferTypeId type, std::vector<std::size_t> loads,
         Integer minimum = 0, Integer maximum = 0)
        : position(point), kind(NodeKind::buffer), buffer_type(type),
          children(std::move(loads)), low(minimum), high(maximum) {}
    bool is_buffer() const { return kind == NodeKind::buffer; }
    bool is_sink() const { return kind == NodeKind::sink; }
    std::size_t type_index() const { return buffer_type.value; }
    void set_buffer_type(BufferTypeId type) { buffer_type = type; }
    // Preserve the established deterministic candidate ordering.
    Integer ordering_key() const {
        return is_buffer() ? static_cast<Integer>(buffer_type.value) : (is_sink() ? -2 : -1);
    }
};
using Tree = std::vector<Node>;

Integer add(Integer a, Integer b);
Integer distance(Position a, Position b);
Tree initial_tree(Problem const& p, bool parity);
Metrics validate_tree(Problem const& p, Tree const& tree);
int cheapest_buffer_type(Problem const& p, std::size_t fanout, Integer length);
void select_buffer_types(Problem const& p, Tree& tree);
}
