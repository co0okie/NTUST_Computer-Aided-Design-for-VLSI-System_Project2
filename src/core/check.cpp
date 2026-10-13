#include "core/check.hpp"
#include <algorithm>
#include <set>

namespace cbi {
std::optional<Integer> checked_add(Integer a, Integer b) {
    if (a < 0 || b < 0 || a > std::numeric_limits<Integer>::max() - b) return std::nullopt;
    return a + b;
}
std::optional<Integer> checked_distance(Position a, Position b) {
    if (a.x < 0 || a.y < 0 || b.x < 0 || b.y < 0) return std::nullopt;
    return checked_add(std::max(a.x, b.x) - std::min(a.x, b.x),
                       std::max(a.y, b.y) - std::min(a.y, b.y));
}
Expected<Topology> check_topology(Problem const& p, Tree const& tree) {
    if (p.sinks.empty() || tree.size() < p.sinks.size() + 1)
        return Error{"tree omits pins", ErrorKind::structure};
    for (std::size_t i = 0; i < tree.size(); ++i) {
        auto const& n = tree[i];
        if (i <= p.sinks.size()) {
            auto const expected = i == 0 ? p.source : p.sinks[i - 1];
            if (n.position.x != expected.x || n.position.y != expected.y ||
                n.kind != (i == 0 ? NodeKind::source : NodeKind::sink))
                return Error{"tree changes an input pin", ErrorKind::structure};
        } else if (!n.is_buffer() || n.type_index() >= p.buffer_library.size() || n.children.empty()) {
            return Error{"invalid buffer", ErrorKind::structure};
        }
        if (n.is_sink() && !n.children.empty()) return Error{"sink has children", ErrorKind::structure};
    }
    Topology result{std::vector<NodeId>(tree.size(), NodeId{0}), {NodeId{0}}};
    std::vector<bool> seen(tree.size()); seen[0] = true;
    for (std::size_t k = 0; k < result.order.size(); ++k) {
        auto const u = result.order[k].value;
        for (auto v : tree[u].children) {
            if (v >= tree.size() || seen[v]) return Error{"invalid tree edge or repeated child", ErrorKind::structure};
            seen[v] = true; result.parents[v] = NodeId{u}; result.order.push_back(NodeId{v});
        }
    }
    if (result.order.size() != tree.size()) return Error{"unreachable components", ErrorKind::structure};
    return result;
}
Expected<Metrics> evaluate_tree(Problem const& p, Tree const& tree) {
    auto const topology = check_topology(p, tree);
    if (is_error(topology)) return std::get<Error>(topology);
    std::set<std::pair<Integer, Integer>> occupied;
    Integer cost = 0;
    for (auto const& n : tree) {
        if (!occupied.emplace(n.position.x, n.position.y).second)
            return Error{"overlapping components", ErrorKind::geometry};
        if (n.is_buffer()) {
            if (n.position.x <= 0 || n.position.y <= 0 || n.position.x >= p.dimensions.x || n.position.y >= p.dimensions.y)
                return Error{"invalid buffer", ErrorKind::geometry};
            auto const next = checked_add(cost, p.buffer_library[n.type_index()].cost);
            if (!next) return Error{"integer range exceeded", ErrorKind::overflow};
            cost = *next;
        }
    }
    std::vector<Integer> arrivals(tree.size());
    for (auto id : std::get<Topology>(topology).order) {
        auto const u = id.value; auto const& n = tree[u];
        auto const fanout = u == 0 ? p.source_limits.fanout : (n.is_buffer() ? p.buffer_library[n.type_index()].fanout : 0);
        auto const limit = u == 0 ? p.source_limits.length : (n.is_buffer() ? p.buffer_library[n.type_index()].length : 0);
        if (fanout < 0 || n.children.size() > static_cast<std::uint64_t>(fanout))
            return Error{"fanout exceeds limit", ErrorKind::capacity};
        Integer wire = 0;
        for (auto v : n.children) {
            auto const d = checked_distance(n.position, tree[v].position);
            auto const length = d ? checked_add(wire, *d) : std::nullopt;
            auto const arrival = d ? checked_add(arrivals[u], *d) : std::nullopt;
            if (!length || !arrival) return Error{"integer range exceeded", ErrorKind::overflow};
            wire = *length; arrivals[v] = *arrival;
        }
        if (wire > limit) return Error{"outgoing length exceeds limit", ErrorKind::capacity};
    }
    auto const bounds = std::minmax_element(arrivals.begin() + 1, arrivals.begin() + static_cast<std::ptrdiff_t>(p.sinks.size() + 1));
    auto const skew = *bounds.second - *bounds.first;
    if (p.skew_weight < 0 || (p.skew_weight && skew > std::numeric_limits<Integer>::max() / p.skew_weight))
        return Error{"score exceeds integer range", ErrorKind::overflow};
    auto const score = checked_add(cost, skew * p.skew_weight);
    if (!score) return Error{"integer range exceeded", ErrorKind::overflow};
    return Metrics{*bounds.second, *bounds.first, *score};
}
std::optional<BufferTypeId> feasible_type(Problem const& p, std::size_t fanout, Integer length) {
    std::optional<BufferTypeId> best;
    for (std::size_t i = 0; i < p.buffer_library.size(); ++i) {
        auto const& t = p.buffer_library[i];
        if (t.fanout >= 0 && fanout <= static_cast<std::uint64_t>(t.fanout) && length <= t.length &&
            (!best || t.cost < p.buffer_library[best->value].cost)) best = BufferTypeId{i};
    }
    return best;
}
Expected<WriteSuccess> normalize_types(Problem const& p, Tree& tree) {
    // Resolve all choices before committing any of them.
    std::vector<std::pair<NodeId, BufferTypeId>> choices;
    for (std::size_t u = p.sinks.size() + 1; u < tree.size(); ++u) {
        Integer wire = 0;
        for (auto v : tree[u].children) {
            if (v >= tree.size()) return Error{"invalid tree edge", ErrorKind::structure};
            auto const d = checked_distance(tree[u].position, tree[v].position);
            auto const next = d ? checked_add(wire, *d) : std::nullopt;
            if (!next) return Error{"integer range exceeded", ErrorKind::overflow};
            wire = *next;
        }
        auto const type = feasible_type(p, tree[u].children.size(), wire);
        if (!type) return Error{"no legal buffer type", ErrorKind::capacity};
        choices.push_back({NodeId{u}, *type});
    }
    for (auto const& choice : choices) tree[choice.first.value].set_buffer_type(choice.second);
    return WriteSuccess{};
}
}
