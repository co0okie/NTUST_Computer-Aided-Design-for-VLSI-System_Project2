#include "core/tree.hpp"
#include "core/check.hpp"
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
    return add(std::max(a.x, b.x) - std::min(a.x, b.x), std::max(a.y, b.y) - std::min(a.y, b.y));
}

Tree initial_tree(Problem const &p, bool parity) {
    Tree tree{{p.source, NodeKind::source, {}}};
    for (auto s : p.sinks) {
        Integer const offset = parity ? ((s.x % 2 + s.y % 2) % 2) : 0;
        tree.push_back({s, NodeKind::sink, {}, offset, offset});
    }
    return tree;
}

Metrics validate_tree(Problem const &p, Tree const &tree) {
    auto const result = evaluate_tree(p, tree);
    if (is_error(result))
        throw std::runtime_error(std::get<Error>(result).message);
    return std::get<Metrics>(result);
}

int cheapest_buffer_type(Problem const &p, std::size_t fanout, Integer length) {
    int cheapest = -1;
    for (std::size_t j = 0; j < p.buffer_library.size(); ++j) {
        auto const &type = p.buffer_library[j];
        if (static_cast<std::uint64_t>(type.fanout) >= fanout && type.length >= length &&
            (cheapest < 0 || type.cost < p.buffer_library[static_cast<std::size_t>(cheapest)].cost))
            cheapest = static_cast<int>(j);
    }
    if (cheapest < 0)
        throw std::runtime_error("no legal buffer type");
    return cheapest;
}

void select_buffer_types(Problem const &p, Tree &tree) {
    for (std::size_t i = p.sinks.size() + 1; i < tree.size(); ++i) {
        Integer length = 0;
        for (auto child : tree[i].children)
            length = add(length, distance(tree[i].position, tree[child].position));
        tree[i].set_buffer_type(BufferTypeId{
            static_cast<std::size_t>(cheapest_buffer_type(p, tree[i].children.size(), length))});
    }
}

} // namespace cbi
