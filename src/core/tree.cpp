#include "core/tree.hpp"
#include <algorithm>
#include <functional>
#include <limits>
#include <numeric>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <tuple>


namespace cbi {
Integer add(Integer a, Integer b) {
    if (a < 0 || b < 0 || a > std::numeric_limits<Integer>::max() - b)
        throw std::runtime_error("integer range exceeded");
    return a + b;
}

Integer distance(Position a, Position b) {
    return add(std::max(a.x, b.x) - std::min(a.x, b.x),
               std::max(a.y, b.y) - std::min(a.y, b.y));
}

Tree initial_tree(Problem const& p, bool parity) {
    Tree tree{{p.source, -1, {}}};
    for (auto s : p.sinks) {
        Integer const offset = parity ? ((s.x % 2 + s.y % 2) % 2) : 0;
        tree.push_back({s, -2, {}, offset, offset});
    }
    return tree;
}

Metrics validate_tree(Problem const& p, Tree const& tree) {
    if (tree.size() < p.sinks.size() + 1)
        throw std::runtime_error("tree omits pins");
    std::set<std::pair<Integer, Integer>> occupied;
    Integer cost = 0;
    for (std::size_t i = 0; i < tree.size(); ++i) {
        auto const& n = tree[i];
        if (!occupied.emplace(n.position.x, n.position.y).second)
            throw std::runtime_error("overlapping components");
        if (i == 0 || i <= p.sinks.size()) {
            auto const expected = i == 0 ? p.source : p.sinks[i - 1];
            if (n.position.x != expected.x || n.position.y != expected.y ||
                n.type != (i == 0 ? -1 : -2))
                throw std::runtime_error("tree changes an input pin");
        } else {
            if (n.type < 0 || static_cast<std::size_t>(n.type) >= p.buffer_library.size() ||
                n.position.x <= 0 || n.position.y <= 0 ||
                n.position.x >= p.dimensions.x || n.position.y >= p.dimensions.y ||
                n.children.empty())
                throw std::runtime_error("invalid buffer");
            cost = add(cost, p.buffer_library[static_cast<std::size_t>(n.type)].cost);
        }
        if (n.type == -2 && !n.children.empty())
            throw std::runtime_error("sink has children");
    }
    std::vector<bool> seen(tree.size());
    std::vector<Integer> arrival(tree.size());
    std::vector<std::size_t> queue{0};
    seen[0] = true;
    for (std::size_t k = 0; k < queue.size(); ++k) {
        auto const u = queue[k];
        auto const& n = tree[u];
        auto const fanout = u == 0 ? p.source_limits.fanout :
            (n.type >= 0 ? p.buffer_library[static_cast<std::size_t>(n.type)].fanout : 0);
        auto const limit = u == 0 ? p.source_limits.length :
            (n.type >= 0 ? p.buffer_library[static_cast<std::size_t>(n.type)].length : 0);
        if (n.children.size() > static_cast<std::uint64_t>(fanout))
            throw std::runtime_error("fanout exceeds limit");
        Integer length = 0;
        for (auto v : n.children) {
            if (v >= tree.size() || seen[v])
                throw std::runtime_error("invalid tree edge or repeated child");
            seen[v] = true;
            auto const d = distance(n.position, tree[v].position);
            length = add(length, d);
            arrival[v] = add(arrival[u], d);
            queue.push_back(v);
        }
        if (length > limit) throw std::runtime_error("outgoing length exceeds limit");
    }
    if (queue.size() != tree.size()) throw std::runtime_error("unreachable components");
    auto const bounds = std::minmax_element(arrival.begin() + 1,
        arrival.begin() + static_cast<std::ptrdiff_t>(p.sinks.size() + 1));
    Integer const skew = *bounds.second - *bounds.first;
    if (p.skew_weight != 0 && skew > std::numeric_limits<Integer>::max() / p.skew_weight)
        throw std::runtime_error("score exceeds integer range");
    return {*bounds.second, *bounds.first, add(cost, skew * p.skew_weight)};
}

int cheapest_buffer_type(Problem const& p, std::size_t fanout, Integer length) {
    int cheapest = -1;
    for (std::size_t j = 0; j < p.buffer_library.size(); ++j) {
        auto const& type = p.buffer_library[j];
        if (static_cast<std::uint64_t>(type.fanout) >= fanout && type.length >= length &&
            (cheapest < 0 || type.cost < p.buffer_library[static_cast<std::size_t>(cheapest)].cost))
            cheapest = static_cast<int>(j);
    }
    if (cheapest < 0) throw std::runtime_error("no legal buffer type");
    return cheapest;
}

void select_buffer_types(Problem const& p, Tree& tree) {
    for (std::size_t i = p.sinks.size() + 1; i < tree.size(); ++i) {
        Integer length = 0;
        for (auto child : tree[i].children)
            length = add(length, distance(tree[i].position, tree[child].position));
        tree[i].type = cheapest_buffer_type(p, tree[i].children.size(), length);
    }
}


}
