#pragma once

#include "model.hpp"

#include <algorithm>
#include <functional>
#include <limits>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <tuple>

namespace cbi {
using Integer = std::int64_t;
constexpr std::size_t component_budget = 100000;
constexpr Integer chain_distance_budget = 4096;
constexpr std::size_t chain_transition_budget = 262144;

Integer add(Integer a, Integer b) {
    if (a < 0 || b < 0 || a > std::numeric_limits<Integer>::max() - b)
        throw std::runtime_error("integer range exceeded");
    return a + b;
}

Integer distance(Position a, Position b) {
    return add(std::max(a.x, b.x) - std::min(a.x, b.x),
               std::max(a.y, b.y) - std::min(a.y, b.y));
}

struct Node {
    Position position;
    int type; // -1: source, -2: sink; otherwise buffer library index
    std::vector<std::size_t> children;
    Integer low = 0;
    Integer high = 0;
};
using Tree = std::vector<Node>;

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

// Fixed-topology relocation changes only two drivers' wire totals and the
// arrivals below the moved buffer. The snapshot lasts for one search pass.
class RelocationEvaluation {
    struct Bounds {
        Integer low = std::numeric_limits<Integer>::max(), high = 0;
        bool empty = true;
        void include(Integer minimum, Integer maximum) {
            low = std::min(low, minimum); high = std::max(high, maximum); empty = false;
        }
        void include(Bounds const& other) { if (!other.empty) include(other.low, other.high); }
    };
    Problem const& p;
    Tree const& tree;
    std::vector<std::size_t> parents;
    std::vector<Integer> arrivals, wire;
    std::vector<int> types;
    std::vector<Bounds> below, outside;
    std::set<std::pair<Integer, Integer>> occupied;
    Integer cost = 0;

    int driver_type(std::size_t node, Integer length) const {
        if (node != 0) return cheapest_buffer_type(p, tree[node].children.size(), length);
        if (tree[0].children.size() > static_cast<std::uint64_t>(p.source_limits.fanout) ||
            length > p.source_limits.length) throw std::runtime_error("SRC driving limit exceeded");
        return -1;
    }
    Integer price(int type) const { return type < 0 ? 0 : p.buffer_library[static_cast<std::size_t>(type)].cost; }

public:
    RelocationEvaluation(Problem const& problem, Tree const& snapshot)
        : p(problem), tree(snapshot), parents(tree.size()), arrivals(tree.size()), wire(tree.size()),
          types(tree.size(), -1), below(tree.size()), outside(tree.size()) {
        (void)validate_tree(p, tree);
        std::vector<std::size_t> queue{0};
        for (std::size_t k = 0; k < queue.size(); ++k) {
            auto const u = queue[k];
            occupied.emplace(tree[u].position.x, tree[u].position.y);
            for (auto v : tree[u].children) {
                auto const length = distance(tree[u].position, tree[v].position);
                wire[u] = add(wire[u], length); arrivals[v] = add(arrivals[u], length);
                parents[v] = u; queue.push_back(v);
            }
            if (u > p.sinks.size()) {
                types[u] = driver_type(u, wire[u]); cost = add(cost, price(types[u]));
            }
        }
        for (auto it = queue.rbegin(); it != queue.rend(); ++it) {
            auto const u = *it;
            if (u && u <= p.sinks.size()) below[u].include(arrivals[u], arrivals[u]);
            for (auto v : tree[u].children) below[u].include(below[v]);
        }
        for (auto u : queue) {
            auto const& children = tree[u].children;
            std::vector<Bounds> prefix(children.size() + 1);
            for (std::size_t k = 0; k < children.size(); ++k) {
                prefix[k + 1] = prefix[k]; prefix[k + 1].include(below[children[k]]);
            }
            Bounds suffix;
            for (std::size_t k = children.size(); k > 0; --k) {
                auto const v = children[k - 1];
                outside[v] = outside[u]; outside[v].include(prefix[k - 1]); outside[v].include(suffix);
                suffix.include(below[v]);
            }
        }
    }

    std::size_t parent(std::size_t node) const { return parents[node]; }
    bool occupied_at(Position point) const { return occupied.count({point.x, point.y}) != 0; }

    Metrics evaluate(std::size_t moved, Position point) const {
        if (moved <= p.sinks.size() || moved >= tree.size() || point.x <= 0 || point.y <= 0 ||
            point.x >= p.dimensions.x || point.y >= p.dimensions.y ||
            (occupied_at(point) && (point.x != tree[moved].position.x || point.y != tree[moved].position.y)))
            throw std::runtime_error("invalid relocation");
        auto const from = parents[moved];
        auto const incoming = distance(tree[from].position, point);
        auto const parent_wire = add(wire[from] - distance(tree[from].position, tree[moved].position), incoming);
        auto const parent_type = driver_type(from, parent_wire);
        auto const moved_arrival = add(arrivals[from], incoming);
        Integer moved_wire = 0;
        Bounds bounds = outside[moved];
        for (auto child : tree[moved].children) {
            auto const length = distance(point, tree[child].position);
            moved_wire = add(moved_wire, length);
            auto const child_arrival = add(moved_arrival, length);
            bounds.include(add(child_arrival, below[child].low - arrivals[child]),
                           add(child_arrival, below[child].high - arrivals[child]));
        }
        auto const moved_type = driver_type(moved, moved_wire);
        auto const next_cost = add(add(cost - price(types[from]) - price(types[moved]), price(parent_type)), price(moved_type));
        auto const skew = bounds.high - bounds.low;
        if (p.skew_weight && skew > std::numeric_limits<Integer>::max() / p.skew_weight)
            throw std::runtime_error("score exceeds integer range");
        return {bounds.high, bounds.low, add(next_cost, skew * p.skew_weight)};
    }
};

std::size_t local_trial_limit(Problem const& p, Tree const& tree, std::size_t* remaining_trials) {
    auto const limit = std::min(std::size_t{2048},
        std::size_t{2000000} / tree.size() / (p.buffer_library.size() + 16));
    return remaining_trials ? std::min(limit, *remaining_trials) : limit;
}

// Scalable bottom-up contractions for trees excluded from expensive searches.
// Triangle equality preserves every downstream arrival exactly, so a local
// cost reduction cannot trade away skew. Compact and validate only once.
Tree contract_large_tree(Problem const& p, Tree const& original, std::size_t* shared_work = nullptr) {
    if (original.size() <= 512 || original.size() > component_budget ||
        p.buffer_library.size() > 64) return original;
    std::size_t work = shared_work ? std::min(std::size_t{1048576}, *shared_work) : 1048576;
    auto const start = work;
    if (!work) return original;
    auto tree = original;
    std::vector<std::size_t> order{0};
    std::vector<Integer> wire(tree.size(), 0);
    for (std::size_t k = 0; k < order.size(); ++k) {
        auto const parent = order[k];
        for (auto child : tree[parent].children) {
            wire[parent] = add(wire[parent], distance(tree[parent].position, tree[child].position));
            order.push_back(child);
        }
    }
    std::vector<bool> removed(tree.size(), false);
    bool changed = false;
    for (auto it = order.rbegin(); it != order.rend() && work; ++it) {
        auto const parent = *it;
        if (tree[parent].type == -2 || tree[parent].children.size() > 64) continue;
        for (std::size_t slot = 0; slot < tree[parent].children.size() && work;) {
            --work;
            auto const child = tree[parent].children[slot];
            auto const& grandchildren = tree[child].children;
            auto const degree = tree[parent].children.size() - 1 + grandchildren.size();
            if (tree[child].type < 0 || degree > 64) { ++slot; continue; }
            try {
                auto const edge = distance(tree[parent].position, tree[child].position);
                Integer length = wire[parent] - edge;
                bool equal = true;
                for (auto grandchild : grandchildren) {
                    if (!work) { equal = false; break; }
                    --work;
                    auto const direct = distance(tree[parent].position, tree[grandchild].position);
                    if (direct != add(edge, distance(tree[child].position, tree[grandchild].position))) {
                        equal = false; break;
                    }
                    length = add(length, direct);
                }
                if (!equal) { ++slot; continue; }
                auto const old_cost = parent == 0 ? 0 : p.buffer_library[static_cast<std::size_t>(tree[parent].type)].cost;
                auto const saved_cost = add(old_cost, p.buffer_library[static_cast<std::size_t>(tree[child].type)].cost);
                int type = -1;
                Integer cost = 0;
                if (parent == 0) {
                    if (degree > static_cast<std::uint64_t>(p.source_limits.fanout) || length > p.source_limits.length) {
                        ++slot; continue;
                    }
                } else {
                    for (std::size_t t = 0; t < p.buffer_library.size() && work; ++t) {
                        --work;
                        auto const& buf = p.buffer_library[t];
                        if (degree <= static_cast<std::uint64_t>(buf.fanout) && length <= buf.length &&
                            (type < 0 || buf.cost < cost)) { type = static_cast<int>(t); cost = buf.cost; }
                    }
                    if (type < 0 || cost >= saved_cost) { ++slot; continue; }
                }
                auto children = tree[parent].children;
                children.erase(children.begin() + static_cast<std::ptrdiff_t>(slot));
                children.insert(children.begin() + static_cast<std::ptrdiff_t>(slot), grandchildren.begin(), grandchildren.end());
                tree[parent].children = std::move(children);
                if (parent != 0) tree[parent].type = type;
                wire[parent] = length; removed[child] = true; changed = true;
                // Newly exposed grandchildren were already processed bottom-up.
                slot += grandchildren.size();
            } catch (std::runtime_error const&) { ++slot; }
        }
    }
    if (shared_work) *shared_work -= start - work;
    if (!changed) return original;
    Tree compact;
    std::vector<std::size_t> mapping(tree.size());
    for (std::size_t i = 0; i < tree.size(); ++i)
        if (!removed[i]) { mapping[i] = compact.size(); compact.push_back(std::move(tree[i])); }
    for (auto& n : compact)
        for (auto& child : n.children) child = mapping[child];
    try {
        select_buffer_types(p, compact);
        if (validate_tree(p, compact).score < validate_tree(p, original).score) return compact;
    } catch (std::runtime_error const&) {}
    return original;
}

Tree improve_tree(Problem const& p, Tree best, std::size_t* remaining_trials = nullptr) {
    // Best-improvement passes prevent an early skew-increasing contraction
    // from hiding a cheaper balanced merge. Bound both tree size and work.
    if (best.size() > 512) return best;
    auto score = validate_tree(p, best).score;
    auto const trial_limit = local_trial_limit(p, best, remaining_trials);
    std::size_t trials = 0;
    for (unsigned pass = 0; pass < 16 && trials < trial_limit; ++pass) {
        std::optional<Tree> improved;
        auto next_score = score;
        for (std::size_t parent = 0; parent < best.size() && trials < trial_limit; ++parent) {
            for (std::size_t k = 0; k < best[parent].children.size() && trials < trial_limit; ++k) {
                auto const victim = best[parent].children[k];
                if (victim <= p.sinks.size()) continue;
                for (bool relocate : {false, true}) {
                    if (relocate && parent == 0) continue;
                    if (trials >= trial_limit) break;
                    ++trials;
                    if (remaining_trials) --*remaining_trials;
                    Tree candidate = best;
                    auto const children = candidate[victim].children;
                    auto& edges = candidate[parent].children;
                    edges.erase(edges.begin() + static_cast<std::ptrdiff_t>(k));
                    edges.insert(edges.begin() + static_cast<std::ptrdiff_t>(k), children.begin(), children.end());
                    if (relocate) candidate[parent].position = candidate[victim].position;
                    candidate.erase(candidate.begin() + static_cast<std::ptrdiff_t>(victim));
                    for (auto& node : candidate)
                        for (auto& child : node.children) if (child > victim) --child;
                    try {
                        select_buffer_types(p, candidate);
                        auto const metrics = validate_tree(p, candidate);
                        if (metrics.score < next_score) {
                            next_score = metrics.score; improved = std::move(candidate);
                        }
                    } catch (std::runtime_error const&) {
                        // Illegal merges leave the original complete tree intact.
                    }
                }
            }
        }
        if (!improved) break;
        best = std::move(*improved); score = next_score;
    }
    return best;
}

Tree relocate_tree(Problem const& p, Tree best, std::size_t* remaining_trials = nullptr) {
    if (best.size() > 512) return best;
    auto score = validate_tree(p, best).score;
    auto const trial_limit = local_trial_limit(p, best, remaining_trials);
    std::size_t trials = 0;
    for (unsigned pass = 0; pass < 8 && trials < trial_limit; ++pass) {
        std::optional<Tree> improved;
        auto next_score = score;
        RelocationEvaluation const evaluation(p, best);
        for (std::size_t i = p.sinks.size() + 1; i < best.size() && trials < trial_limit; ++i) {
            auto const origin = best[i].position;
            std::vector<Position> positions;
            auto offer = [&](Position point) {
                if (positions.size() >= 32 || point.x <= 0 || point.y <= 0 ||
                    point.x >= p.dimensions.x || point.y >= p.dimensions.y ||
                    evaluation.occupied_at(point)) return;
                if (std::none_of(positions.begin(), positions.end(), [&](auto q) {
                        return q.x == point.x && q.y == point.y;
                    })) positions.push_back(point);
            };
            for (Integer dx = -2; dx <= 2; ++dx)
                for (Integer dy = -2; dy <= 2; ++dy) {
                    if ((dx > 0 && origin.x > std::numeric_limits<Integer>::max() - dx) ||
                        (dy > 0 && origin.y > std::numeric_limits<Integer>::max() - dy)) continue;
                    offer({origin.x + dx, origin.y + dy});
                }
            auto align = [&](Position anchor) {
                offer({anchor.x, origin.y}); offer({origin.x, anchor.y});
                offer({origin.x + (anchor.x - origin.x) / 2,
                       origin.y + (anchor.y - origin.y) / 2});
            };
            align(best[evaluation.parent(i)].position);
            for (auto child : best[i].children) align(best[child].position);
            for (auto point : positions) {
                if (trials == trial_limit) break;
                ++trials;
                if (remaining_trials) --*remaining_trials;
                try {
                    if (evaluation.evaluate(i, point).score < next_score) {
                        auto candidate = best;
                        candidate[i].position = point;
                        select_buffer_types(p, candidate);
                        auto const metrics = validate_tree(p, candidate);
                        if (metrics.score < next_score) {
                            next_score = metrics.score; improved = std::move(candidate);
                        }
                    }
                } catch (std::runtime_error const&) {}
            }
        }
        if (!improved) break;
        best = std::move(*improved); score = next_score;
    }
    return best;
}

Tree swap_roots(Problem const& p, Tree best, std::size_t* remaining_trials, bool subtrees) {
    if (best.size() > 512 || p.sinks.size() > 128) return best;
    auto score = validate_tree(p, best).score;
    auto const trial_limit = local_trial_limit(p, best, remaining_trials);
    std::size_t trials = 0;
    for (unsigned pass = 0; pass < 8 && trials < trial_limit; ++pass) {
        std::optional<Tree> improved;
        auto next_score = score;
        std::vector<std::size_t> parents(best.size()), slots(best.size());
        for (std::size_t i = 0; i < best.size(); ++i) {
            for (std::size_t k = 0; k < best[i].children.size(); ++k) {
                auto const child = best[i].children[k]; parents[child] = i; slots[child] = k;
            }
        }
        auto ancestor = [&](std::size_t a, std::size_t b) {
            while (b) { if (a == b) return true; b = parents[b]; }
            return false;
        };
        auto const end = subtrees ? best.size() - 1 : p.sinks.size();
        for (std::size_t a = 1; a <= end && trials < trial_limit; ++a) {
            for (std::size_t b = a + 1; b <= end && trials < trial_limit; ++b) {
                if (parents[a] == parents[b]) continue;
                if (subtrees && a <= p.sinks.size() && b <= p.sinks.size()) continue;
                ++trials;
                if (remaining_trials) --*remaining_trials;
                if (subtrees && (ancestor(a, b) || ancestor(b, a))) continue;
                auto candidate = best;
                std::swap(candidate[parents[a]].children[slots[a]], candidate[parents[b]].children[slots[b]]);
                try {
                    select_buffer_types(p, candidate);
                    auto const metrics = validate_tree(p, candidate);
                    if (metrics.score < next_score) {
                        next_score = metrics.score; improved = std::move(candidate);
                    }
                } catch (std::runtime_error const&) {}
            }
        }
        if (!improved) break;
        best = std::move(*improved); score = next_score;
    }
    return best;
}

Tree swap_leaves(Problem const& p, Tree best, std::size_t* remaining_trials = nullptr) {
    return swap_roots(p, std::move(best), remaining_trials, false);
}

Tree swap_subtrees(Problem const& p, Tree best, std::size_t* remaining_trials = nullptr) {
    return swap_roots(p, std::move(best), remaining_trials, true);
}

Tree reparent_leaves(Problem const& p, Tree best, std::size_t* remaining_trials = nullptr) {
    if (best.size() > 512 || p.sinks.size() > 128) return best;
    auto score = validate_tree(p, best).score;
    auto const trial_limit = local_trial_limit(p, best, remaining_trials);
    auto max_fanout = Integer{0};
    for (auto const& type : p.buffer_library) max_fanout = std::max(max_fanout, type.fanout);
    std::size_t trials = 0;
    for (unsigned pass = 0; pass < 8 && trials < trial_limit; ++pass) {
        std::optional<Tree> improved;
        auto next_score = score;
        std::vector<std::size_t> parents(best.size()), slots(best.size());
        for (std::size_t i = 0; i < best.size(); ++i) {
            for (std::size_t k = 0; k < best[i].children.size(); ++k) {
                auto const child = best[i].children[k]; parents[child] = i; slots[child] = k;
            }
        }
        for (std::size_t leaf = 1; leaf <= p.sinks.size() && trials < trial_limit; ++leaf) {
            auto const from = parents[leaf];
            // Keep every existing component connected and every buffer nonempty.
            if (best[from].children.size() == 1) continue;
            for (std::size_t target = 0; target < best.size() && trials < trial_limit; ++target) {
                if (target == from || best[target].type == -2) continue;
                auto const capacity = target == 0 ? p.source_limits.fanout : max_fanout;
                if (best[target].children.size() >= static_cast<std::uint64_t>(capacity)) continue;
                ++trials;
                if (remaining_trials) --*remaining_trials;
                auto candidate = best;
                auto& edges = candidate[from].children;
                edges.erase(edges.begin() + static_cast<std::ptrdiff_t>(slots[leaf]));
                candidate[target].children.push_back(leaf);
                try {
                    select_buffer_types(p, candidate);
                    auto const metrics = validate_tree(p, candidate);
                    if (metrics.score < next_score) {
                        next_score = metrics.score; improved = std::move(candidate);
                    }
                } catch (std::runtime_error const&) {}
            }
        }
        if (!improved) break;
        best = std::move(*improved); score = next_score;
    }
    return best;
}

Tree delay_tree(Problem const& p, Tree best, std::size_t* remaining_trials = nullptr,
                std::size_t* remaining_insertions = nullptr) {
    if (best.size() >= 512 || p.sinks.size() > 128 || p.dimensions.x <= 1 || p.dimensions.y <= 1) return best;
    auto score = validate_tree(p, best).score;
    auto const trial_limit = local_trial_limit(p, best, remaining_trials);
    std::size_t trials = 0;
    for (unsigned pass = 0; pass < 8 && trials < trial_limit && best.size() < 512; ++pass) {
        if (remaining_insertions && !*remaining_insertions) break;
        std::vector<Integer> arrival(best.size()), high(best.size());
        std::vector<std::size_t> queue{0};
        std::set<std::pair<Integer, Integer>> occupied;
        for (std::size_t k = 0; k < queue.size(); ++k) {
            auto const parent = queue[k];
            occupied.emplace(best[parent].position.x, best[parent].position.y);
            for (auto child : best[parent].children) {
                arrival[child] = add(arrival[parent], distance(best[parent].position, best[child].position));
                queue.push_back(child);
            }
        }
        for (auto it = queue.rbegin(); it != queue.rend(); ++it) {
            auto const node = *it;
            high[node] = arrival[node];
            if (!best[node].children.empty()) {
                high[node] = 0;
                for (auto child : best[node].children) {
                    high[node] = std::max(high[node], high[child]);
                }
            }
        }
        std::optional<Tree> improved;
        auto next_score = score;
        for (std::size_t parent = 0; parent < best.size() && trials < trial_limit; ++parent) {
            for (std::size_t k = 0; k < best[parent].children.size() && trials < trial_limit; ++k) {
                auto const child = best[parent].children[k];
                if (high[child] >= high[0]) continue;
                auto const a = best[parent].position, b = best[child].position;
                std::vector<Position> positions;
                auto offer = [&](Position point) {
                    if (positions.size() == 32 || point.x <= 0 || point.y <= 0 ||
                        point.x >= p.dimensions.x || point.y >= p.dimensions.y || occupied.count({point.x, point.y})) return;
                    if (std::none_of(positions.begin(), positions.end(), [&](auto q) {
                            return q.x == point.x && q.y == point.y;
                        })) positions.push_back(point);
                };
                for (auto center : {a, b})
                    for (Integer dx = -1; dx <= 1; ++dx)
                        for (Integer dy = -1; dy <= 1; ++dy) {
                            if ((dx > 0 && center.x > std::numeric_limits<Integer>::max() - dx) ||
                                (dy > 0 && center.y > std::numeric_limits<Integer>::max() - dy)) continue;
                            offer({center.x + dx, center.y + dy});
                        }
                auto const xmin = std::min(a.x, b.x), xmax = std::max(a.x, b.x);
                auto const ymin = std::min(a.y, b.y), ymax = std::max(a.y, b.y);
                auto const gap = high[0] - high[child];
                for (auto excursion : {gap / 2, gap / 2 + gap % 2}) {
                    for (auto y : {a.y, b.y, ymin + (ymax - ymin) / 2}) {
                        y = std::clamp(y, Integer{1}, p.dimensions.y - 1);
                        if (excursion < xmin) offer({xmin - excursion, y});
                        if (excursion < p.dimensions.x - xmax) offer({xmax + excursion, y});
                    }
                    for (auto x : {a.x, b.x, xmin + (xmax - xmin) / 2}) {
                        x = std::clamp(x, Integer{1}, p.dimensions.x - 1);
                        if (excursion < ymin) offer({x, ymin - excursion});
                        if (excursion < p.dimensions.y - ymax) offer({x, ymax + excursion});
                    }
                }
                for (auto point : positions) {
                    if (trials == trial_limit) break;
                    ++trials;
                    if (remaining_trials) --*remaining_trials;
                    try {
                        if (add(distance(a, point), distance(point, b)) <= distance(a, b)) continue;
                        auto candidate = best;
                        candidate[parent].children[k] = candidate.size();
                        candidate.push_back({point, 0, {child}});
                        select_buffer_types(p, candidate);
                        auto const metrics = validate_tree(p, candidate);
                        if (metrics.score < next_score) {
                            next_score = metrics.score; improved = std::move(candidate);
                        }
                    } catch (std::runtime_error const&) {}
                }
            }
        }
        if (!improved) break;
        if (remaining_insertions) --*remaining_insertions;
        best = std::move(*improved); score = next_score;
    }
    return best;
}

// Revisit delay on the final selected tree, after subtree moves and alternative
// candidate comparison. Its separate cap cannot consume established budgets.
Tree final_delay(Problem const& p, Tree best, std::size_t* shared_trials = nullptr,
                 std::size_t* shared_insertions = nullptr) {
    std::size_t trials = shared_trials ? std::min(std::size_t{2048}, *shared_trials) : 2048;
    std::size_t insertions = shared_insertions ? std::min(std::size_t{16}, *shared_insertions) : 16;
    auto const trial_start = trials, insertion_start = insertions;
    best = delay_tree(p, std::move(best), &trials, &insertions);
    if (shared_trials) *shared_trials -= trial_start - trials;
    if (shared_insertions) *shared_insertions -= insertion_start - insertions;
    return best;
}

enum class Move { bypass, relocate, leaves, reparent, delay, subtrees };

struct SearchBudget { std::size_t trials; std::size_t insertions; };

class RefinementScheduler {
    Problem const& p;
    SearchBudget& budget;

    Tree apply(Move move, Tree best, std::size_t& quota, std::size_t slice = 2048) {
        auto allowance = std::min({quota, budget.trials, slice});
        if (!allowance) return best;
        auto const start = allowance;
        switch (move) {
        case Move::bypass: best = improve_tree(p, std::move(best), &allowance); break;
        case Move::relocate: best = relocate_tree(p, std::move(best), &allowance); break;
        case Move::leaves: best = swap_leaves(p, std::move(best), &allowance); break;
        case Move::reparent: best = reparent_leaves(p, std::move(best), &allowance); break;
        case Move::delay: best = delay_tree(p, std::move(best), &allowance, &budget.insertions); break;
        case Move::subtrees: best = swap_subtrees(p, std::move(best), &allowance); break;
        }
        auto const used = start - allowance;
        budget.trials -= used; quota -= used;
        return best;
    }

public:
    RefinementScheduler(Problem const& problem, SearchBudget& limits) : p(problem), budget(limits) {}

    Tree reference(Tree best) {
        // Declarative compatibility schedule preserves every established phase
        // and its result while using the same accounting as adaptive dispatch.
        struct Phase { std::size_t trials; unsigned rounds; std::vector<Move> moves; };
        std::vector<Phase> const phases{
            {4096, 1, {Move::relocate, Move::leaves}},
            {6144, 3, {Move::bypass, Move::relocate, Move::leaves}},
            {8192, 3, {Move::reparent, Move::bypass, Move::relocate, Move::leaves}},
            {10240, 3, {Move::delay, Move::reparent, Move::bypass, Move::relocate, Move::leaves}},
            {8192, 3, {Move::subtrees, Move::bypass, Move::relocate, Move::leaves, Move::reparent}}
        };
        for (auto const& phase : phases) {
            auto quota = std::min(phase.trials, budget.trials);
            for (unsigned round = 0; round < phase.rounds && quota; ++round) {
                auto const score = validate_tree(p, best).score;
                for (auto move : phase.moves) best = apply(move, std::move(best), quota);
                if (validate_tree(p, best).score == score) break;
            }
        }
        return best;
    }

    Tree adaptive(Tree best) {
        if (best.size() > 512 || p.sinks.size() > 128) return best;
        std::vector<Move> const moves{Move::bypass, Move::relocate, Move::leaves,
                                      Move::reparent, Move::subtrees, Move::delay};
        auto pending = moves;
        auto quota = budget.trials;
        auto score = validate_tree(p, best).score;
        for (unsigned dispatch = 0; dispatch < 64 && quota && !pending.empty(); ++dispatch) {
            auto const move = pending.front(); pending.erase(pending.begin());
            best = apply(move, std::move(best), quota, 512);
            auto const next_score = validate_tree(p, best).score;
            if (next_score < score) {
                score = next_score;
                // Any changed arrival/topology can unlock another operator.
                // Keep at most one pending entry per operator, including itself.
                for (auto retry : moves)
                    if (std::find(pending.begin(), pending.end(), retry) == pending.end()) pending.push_back(retry);
            }
        }
        return best;
    }
};

Tree scheduled_refinement(Problem const& p, Tree best, bool adaptive,
                          std::size_t* shared_trials, std::size_t* shared_insertions) {
    auto const cap = adaptive ? std::size_t{8192} : std::size_t{36864};
    SearchBudget budget{shared_trials ? std::min(cap, *shared_trials) : cap,
                        shared_insertions ? std::min(std::size_t{16}, *shared_insertions) : 16};
    auto const initial = budget;
    RefinementScheduler scheduler(p, budget);
    best = adaptive ? scheduler.adaptive(std::move(best)) : scheduler.reference(std::move(best));
    if (shared_trials) *shared_trials -= initial.trials - budget.trials;
    if (shared_insertions) *shared_insertions -= initial.insertions - budget.insertions;
    return best;
}

Tree refine_tree(Problem const& p, Tree best, std::size_t* trials = nullptr, std::size_t* insertions = nullptr) {
    return scheduled_refinement(p, std::move(best), false, trials, insertions);
}

Tree adaptive_refine(Problem const& p, Tree best, std::size_t* trials = nullptr, std::size_t* insertions = nullptr) {
    return scheduled_refinement(p, std::move(best), true, trials, insertions);
}

class Builder {
    Problem const& p;
    Tree tree;
    std::set<std::pair<Integer, Integer>> occupied;
    int branching_type = -1;
    int relay_type = -1;
    bool mixed_relays = false;
    bool shorter_ties = false;
    std::vector<Integer> chain_cost{0};
    std::vector<int> chain_type{-1};
    std::size_t chain_transitions = 0;
    bool optimize_branch_budget = false;
    std::size_t allocation_trials = 0;
    std::size_t construction_limit = component_budget;
    std::size_t* construction_work = nullptr;
    bool charge_geometry_edges = false;

    // Minimum buffer cost to cover the remaining distance without placement
    // obstacles: C(r) = min_t(cost_t + C(max(0, r - length_t))). Geometry is
    // checked separately and each actual step updates the remaining distance.
    int relay_for(Integer remaining) {
        if (!mixed_relays || remaining > chain_distance_budget) return relay_type;
        auto const extent = static_cast<std::size_t>(remaining);
        auto const needed = extent + 1 > chain_cost.size() ? extent + 1 - chain_cost.size() : 0;
        if (needed > (chain_transition_budget - chain_transitions) / p.buffer_library.size()) return relay_type;
        while (chain_cost.size() <= extent) {
            Integer const r = static_cast<Integer>(chain_cost.size());
            Integer best = std::numeric_limits<Integer>::max();
            int chosen = -1;
            for (std::size_t i = 0; i < p.buffer_library.size(); ++i) {
                auto const& type = p.buffer_library[i];
                auto const tail = static_cast<std::size_t>(std::max(Integer{0}, r - type.length));
                if ((tail != 0 && chain_type[tail] < 0) || type.cost >
                    std::numeric_limits<Integer>::max() - chain_cost[tail]) continue;
                auto const cost = type.cost + chain_cost[tail];
                bool tie = false;
                if (chosen >= 0 && cost == best) {
                    auto const length = p.buffer_library[static_cast<std::size_t>(chosen)].length;
                    tie = shorter_ties ? type.length < length : type.length > length;
                }
                if (chosen < 0 || cost < best || tie) { best = cost; chosen = static_cast<int>(i); }
            }
            chain_transitions += p.buffer_library.size();
            chain_cost.push_back(best); chain_type.push_back(chosen);
        }
        return chain_type[extent] >= 0 ? chain_type[extent] : relay_type;
    }

    bool free(Position s) const {
        return s.x > 0 && s.y > 0 && s.x < p.dimensions.x &&
            s.y < p.dimensions.y && !occupied.count({s.x, s.y});
    }
    std::size_t insert(Position s, int type, std::vector<std::size_t> children = {}) {
        if (tree.size() >= construction_limit)
            throw std::runtime_error("construction component budget exceeded");
        if (construction_work) {
            if (!*construction_work) throw std::runtime_error("construction work budget exceeded");
            --*construction_work;
        }
        occupied.emplace(s.x, s.y);
        tree.push_back({s, type, std::move(children)});
        return tree.size() - 1;
    }

    Position merge_position(std::size_t a, std::size_t b) const {
        auto const& na = tree[a];
        auto const& nb = tree[b];
        Position const pa = na.position, pb = nb.position;
        std::vector<Position> candidates;
        auto offer = [&](Integer x, Integer y) {
            for (Integer dx = -2; dx <= 2; ++dx)
                for (Integer dy = -2; dy <= 2; ++dy)
                    if (x >= 2 && y >= 2 && x <= p.dimensions.x - 2 && y <= p.dimensions.y - 2)
                        candidates.push_back({x + dx, y + dy});
            candidates.push_back({x, y});
        };
        offer(pa.x, pa.y); offer(pb.x, pb.y);
        offer(pa.x, pb.y); offer(pb.x, pa.y);
        offer(pa.x + (pb.x - pa.x) / 2, pa.y + (pb.y - pa.y) / 2);
        offer(std::clamp(pa.x, Integer{1}, p.dimensions.x - 1),
              std::clamp(pa.y, Integer{1}, p.dimensions.y - 1));
        // Intersections of two Manhattan diamond boundaries in u=x+y, v=x-y.
        // Equal virtual arrivals allow skew 0 for uniform parity, 1 otherwise.
        if (na.high > std::numeric_limits<Integer>::max() / 16 ||
            nb.high > std::numeric_limits<Integer>::max() / 16)
            throw std::runtime_error("arrival exceeds construction range");
        Integer const d = distance(pa, pb);
        Integer const sum = add(add(d, na.high), nb.high);
        Integer const start = std::max({na.high, nb.high, sum / 2 + sum % 2});
        for (Integer extra = 0; extra <= 8; ++extra) {
            Integer const t = add(start, extra);
            Integer const ra = t - na.high, rb = t - nb.high;
            Integer const ua = pa.x + pa.y, va = pa.x - pa.y;
            Integer const ub = pb.x + pb.y, vb = pb.x - pb.y;
            auto intersection = [&](Integer u, Integer v) {
                if ((u % 2 + 2) % 2 != (v % 2 + 2) % 2) return;
                Position s{(u + v) / 2, (u - v) / 2};
                if (distance(s, pa) == ra && distance(s, pb) == rb)
                    candidates.push_back(s);
            };
            for (Integer sign : {-1, 1}) {
                Integer const u = ua + sign * ra;
                Integer lo = std::max(va - ra, vb - rb);
                Integer hi = std::min(va + ra, vb + rb);
                // Clip the transformed segment to strictly interior chip points.
                lo = std::max({lo, 2 - u, u - 2 * (p.dimensions.y - 1)});
                hi = std::min({hi, 2 * (p.dimensions.x - 1) - u, u - 2});
                if (u >= ub - rb && u <= ub + rb && lo <= hi) {
                    for (Integer v : {lo, hi, lo + (hi - lo) / 2})
                        for (Integer delta = -3; delta <= 3; ++delta)
                            if (v + delta >= lo && v + delta <= hi) intersection(u, v + delta);
                }
                Integer const v = va + sign * ra;
                lo = std::max(ua - ra, ub - rb);
                hi = std::min(ua + ra, ub + rb);
                lo = std::max({lo, 2 - v, 2 + v});
                hi = std::min({hi, 2 * (p.dimensions.x - 1) - v,
                               2 * (p.dimensions.y - 1) + v});
                if (v >= vb - rb && v <= vb + rb && lo <= hi) {
                    for (Integer u2 : {lo, hi, lo + (hi - lo) / 2})
                        for (Integer delta = -3; delta <= 3; ++delta)
                            if (u2 + delta >= lo && u2 + delta <= hi) intersection(u2 + delta, v);
                }
                for (Integer sign2 : {-1, 1}) {
                    intersection(u, vb + sign2 * rb);
                    intersection(ub + sign2 * rb, v);
                }
            }
        }
        std::optional<Position> best;
        std::tuple<Integer, Integer, Integer> best_key;
        for (auto s : candidates) {
            if (!free(s)) continue;
            auto const da = distance(s, pa), db = distance(s, pb);
            auto const high = std::max(add(da, na.high), add(db, nb.high));
            auto const low = std::min(add(da, na.low), add(db, nb.low));
            auto const key = std::make_tuple(high - low, high, add(da, db));
            if (!best || key < best_key) { best = s; best_key = key; }
        }
        if (!best) throw std::runtime_error("no free merge position found");
        return *best;
    }

    // Find a free relay on a shortest path, using the largest legal step first.
    Position relay_position(Position parent, Position target, Integer budget, bool allow_detour) const {
        if (budget < 1) throw std::runtime_error("insufficient outgoing length for routing");
        Integer const dx = std::max(parent.x, target.x) - std::min(parent.x, target.x);
        Integer const dy = std::max(parent.y, target.y) - std::min(parent.y, target.y);
        Integer const sx = target.x >= parent.x ? 1 : -1;
        Integer const sy = target.y >= parent.y ? 1 : -1;
        for (Integer back = 0; back < 32 && budget - back > 0; ++back) {
            Integer const step = budget - back;
            Integer const lo = std::max(Integer{0}, step - dy);
            Integer const hi = std::min(step, dx);
            if (lo > hi) continue;
            for (Integer amount : {lo, hi, lo + (hi - lo) / 2}) {
                for (Integer delta = -2; delta <= 2; ++delta) {
                    Integer const xstep = amount + delta;
                    if (xstep < lo || xstep > hi) continue;
                    Position s{parent.x + sx * xstep, parent.y + sy * (step - xstep)};
                    if (free(s)) return s;
                }
            }
        }
        // Enter the chip for boundary-aligned edges, or escape an occupied
        // shortest path when routing a common source prefix permits detours.
        if (allow_detour || parent.x == 0 || parent.y == 0 || parent.x == p.dimensions.x ||
            parent.y == p.dimensions.y) {
            auto clip_x = [&](Integer x) { return std::clamp(x, Integer{1}, p.dimensions.x - 1); };
            auto clip_y = [&](Integer y) { return std::clamp(y, Integer{1}, p.dimensions.y - 1); };
            std::optional<Position> best;
            Integer best_distance = std::numeric_limits<Integer>::max();
            for (auto x : {clip_x(parent.x), clip_x(target.x),
                           clip_x(parent.x - budget + 1), clip_x(parent.x + budget - 1)}) {
                for (auto y : {clip_y(parent.y), clip_y(target.y),
                               clip_y(parent.y - budget + 1), clip_y(parent.y + budget - 1)}) {
                    for (Integer ox = -2; ox <= 2; ++ox) {
                        for (Integer oy = -2; oy <= 2; ++oy) {
                            Position const candidate{x + ox, y + oy};
                            if (!free(candidate) || distance(parent, candidate) > budget) continue;
                            auto const remaining = distance(candidate, target);
                            if (!best || remaining < best_distance) {
                                best = candidate; best_distance = remaining;
                            }
                        }
                    }
                }
            }
            if (best) return *best;
        }
        throw std::runtime_error("no free relay position found");
    }

    std::size_t route(Position parent, std::size_t child, Integer budget, bool allow_detour = false) {
        Position const target = tree[child].position;
        std::optional<std::size_t> first, last;
        Integer const capacity = p.buffer_library[static_cast<std::size_t>(relay_type)].length;
        auto const length = distance(parent, target);
        if (length > budget) {
            Integer const excess = length - budget;
            auto const necessary = excess / capacity + (excess % capacity != 0 ? 1 : 0);
            if (tree.size() >= construction_limit ||
                static_cast<std::uint64_t>(necessary) > construction_limit - tree.size())
                throw std::runtime_error("routing exceeds construction component budget");
        }
        while (distance(parent, target) > budget) {
            auto const next = relay_position(parent, target, budget, allow_detour);
            auto const type = relay_for(distance(next, target));
            auto const relay = insert(next, type);
            if (last) tree[*last].children = {relay};
            else first = relay;
            last = relay;
            parent = next;
            budget = p.buffer_library[static_cast<std::size_t>(type)].length;
        }
        if (last) tree[*last].children = {child};
        return first.value_or(child);
    }

    void allocate_branch(std::size_t root, std::size_t a, std::size_t b, Integer limit) {
        auto const s = tree[root].position;
        auto const da = distance(s, tree[a].position), db = distance(s, tree[b].position);
        auto const total = std::min(limit, add(da, db));
        auto const lo = std::max(Integer{1}, total - db);
        auto const hi = std::min(da, total - 1);
        if (lo > hi) return;
        std::set<Integer> budgets;
        auto offer = [&](Integer x) { if (x >= lo && x <= hi) budgets.insert(x); };
        offer(lo); offer(hi); offer(std::clamp(total / 2, lo, hi));
        if (hi - lo <= 64) {
            for (auto x = lo; x <= hi; ++x) offer(x);
        } else {
            // Sample coverage-cost jumps near the middle and both ends.
            // No loop or allocation scales with a potentially huge length.
            for (std::size_t i = 0; i < std::min(p.buffer_library.size(), std::size_t{8}); ++i) {
                auto const cap = p.buffer_library[i].length;
                for (auto center : {lo, hi, lo + (hi - lo) / 2}) {
                    for (auto remainder : {da % cap, (total - db) % cap}) {
                        if (remainder < 0) remainder += cap;
                        auto x = remainder;
                        if (center >= remainder) x += ((center - remainder) / cap) * cap;
                        offer(x); if (x > lo) offer(x - 1);
                        if (cap <= hi - x) { offer(x + cap); if (x + cap > lo) offer(x + cap - 1); }
                    }
                }
            }
        }
        auto estimate = [&](Integer remaining) {
            if (remaining <= 0) return Integer{0};
            auto const type = relay_for(remaining);
            if (mixed_relays && remaining < static_cast<Integer>(chain_cost.size()) &&
                chain_type[static_cast<std::size_t>(remaining)] >= 0)
                return chain_cost[static_cast<std::size_t>(remaining)];
            auto const& buffer = p.buffer_library[static_cast<std::size_t>(type)];
            auto const count = remaining / buffer.length + (remaining % buffer.length != 0);
            if (count > std::numeric_limits<Integer>::max() / buffer.cost)
                return std::numeric_limits<Integer>::max();
            return count * buffer.cost;
        };
        std::vector<std::pair<Integer, Integer>> ranked;
        for (auto ba : budgets) {
            auto const ca = estimate(da - ba), cb = estimate(db - (total - ba));
            auto const cost = ca > std::numeric_limits<Integer>::max() - cb ?
                std::numeric_limits<Integer>::max() : ca + cb;
            ranked.emplace_back(cost, ba);
        }
        std::sort(ranked.begin(), ranked.end());
        std::optional<Builder> best;
        Integer best_cost = std::numeric_limits<Integer>::max();
        auto path_length = [&](Tree const& routed, std::size_t child, std::size_t target) {
            Integer length = distance(s, routed[child].position);
            while (child != target) {
                if (routed[child].children.size() != 1) throw std::runtime_error("invalid branch relay chain");
                auto const next = routed[child].children.front();
                length = add(length, distance(routed[child].position, routed[next].position)); child = next;
            }
            return length;
        };
        std::size_t evaluated = 0;
        for (auto const& entry : ranked) {
            if (evaluated == 8 || allocation_trials == 128) break;
            ++evaluated; ++allocation_trials;
            auto trial = *this;
            trial.construction_limit = 512;
            try {
                auto const ca = trial.route(s, a, entry.second);
                auto const cb = trial.route(s, b, total - entry.second);
                // Keep child arrival offsets valid; boundary detours fall
                // back to the original complete construction candidate.
                if (path_length(trial.tree, ca, a) != da || path_length(trial.tree, cb, b) != db) continue;
                trial.tree[root].children = {ca, cb};
                select_buffer_types(p, trial.tree);
                Integer cost = 0;
                for (std::size_t i = p.sinks.size() + 1; i < trial.tree.size(); ++i)
                    cost = add(cost, p.buffer_library[static_cast<std::size_t>(trial.tree[i].type)].cost);
                if (!best || cost < best_cost) { best_cost = cost; best.emplace(std::move(trial)); }
            } catch (std::runtime_error const&) {}
        }
        if (best) {
            tree = std::move(best->tree); occupied = std::move(best->occupied);
            chain_cost = std::move(best->chain_cost); chain_type = std::move(best->chain_type);
            chain_transitions = best->chain_transitions;
        }
    }

    void spatial_sort(std::vector<std::size_t>& nodes) const {
        Integer xmin = p.dimensions.x, xmax = 0, ymin = p.dimensions.y, ymax = 0;
        for (auto node : nodes) {
            auto const s = tree[node].position;
            xmin = std::min(xmin, s.x); xmax = std::max(xmax, s.x);
            ymin = std::min(ymin, s.y); ymax = std::max(ymax, s.y);
        }
        bool const xaxis = xmax - xmin >= ymax - ymin;
        std::sort(nodes.begin(), nodes.end(), [&](auto a, auto b) {
            auto const pa = tree[a].position, pb = tree[b].position;
            return xaxis ? std::tie(pa.x, pa.y) < std::tie(pb.x, pb.y) :
                           std::tie(pa.y, pa.x) < std::tie(pb.y, pb.x);
        });
    }

    std::vector<std::vector<std::size_t>> spatial_groups(std::vector<std::size_t> nodes, std::size_t count) const {
        std::vector<std::vector<std::size_t>> groups{std::move(nodes)};
        while (groups.size() < count) {
            auto it = std::max_element(groups.begin(), groups.end(), [](auto const& a, auto const& b) {
                return a.size() < b.size();
            });
            spatial_sort(*it);
            auto const middle = it->begin() + static_cast<std::ptrdiff_t>(it->size() / 2);
            std::vector<std::size_t> right(middle, it->end());
            it->erase(middle, it->end()); groups.push_back(std::move(right));
        }
        return groups;
    }

    std::vector<std::vector<std::size_t>> proximity_groups(std::vector<std::size_t> nodes, std::size_t count) {
        auto charge = [&] {
            if (!construction_work || !*construction_work) throw std::runtime_error("construction work budget exceeded");
            --*construction_work;
        };
        auto separation = [&](Position a, Position b) { charge(); return distance(a, b); };
        std::vector<Position> centers;
        // Farthest-first seeds, with coordinate tie breaks independent of pin IDs.
        while (centers.size() < count) {
            std::optional<Position> chosen;
            std::tuple<Integer, Integer, Integer> best_key;
            for (auto node : nodes) {
                auto const point = tree[node].position;
                Integer nearest = centers.empty() ? separation(point, p.source) : std::numeric_limits<Integer>::max();
                for (auto center : centers) nearest = std::min(nearest, separation(point, center));
                auto const key = std::make_tuple(nearest, point.x, point.y);
                if (!chosen || key > best_key) { chosen = point; best_key = key; }
            }
            centers.push_back(*chosen);
        }
        std::vector<std::vector<std::size_t>> groups;
        for (unsigned iteration = 0; iteration < 16; ++iteration) {
            groups.assign(count, {});
            for (auto node : nodes) {
                std::size_t closest = 0; auto nearest = std::numeric_limits<Integer>::max();
                for (std::size_t i = 0; i < centers.size(); ++i) {
                    auto const d = separation(tree[node].position, centers[i]);
                    if (d < nearest) { nearest = d; closest = i; }
                }
                groups[closest].push_back(node);
            }
            bool changed = false;
            for (std::size_t i = 0; i < groups.size(); ++i) {
                if (groups[i].empty()) throw std::runtime_error("empty proximity cluster");
                std::vector<Integer> xs, ys;
                for (auto node : groups[i]) { xs.push_back(tree[node].position.x); ys.push_back(tree[node].position.y); }
                std::sort(xs.begin(), xs.end()); std::sort(ys.begin(), ys.end());
                Position const center{xs[xs.size() / 2], ys[ys.size() / 2]};
                changed = changed || center.x != centers[i].x || center.y != centers[i].y;
                centers[i] = center;
            }
            if (!changed) break;
        }
        return groups;
    }

    Position multiway_position(std::vector<std::size_t> const& children, Integer limit) {
        std::vector<Integer> xs, ys;
        for (auto child : children) { xs.push_back(tree[child].position.x); ys.push_back(tree[child].position.y); }
        std::sort(xs.begin(), xs.end()); std::sort(ys.begin(), ys.end());
        std::set<std::pair<Integer, Integer>> points;
        auto offer = [&](Integer x, Integer y) {
            for (Integer dx = -1; dx <= 1; ++dx)
                for (Integer dy = -1; dy <= 1; ++dy) points.emplace(x + dx, y + dy);
        };
        auto const xmid = xs.front() + (xs.back() - xs.front()) / 2;
        auto const ymid = ys.front() + (ys.back() - ys.front()) / 2;
        for (Integer x : {xmid, xmid + (xs.back() - xs.front()) % 2})
            for (Integer y : {ymid, ymid + (ys.back() - ys.front()) % 2}) offer(x, y);
        offer(xs[(xs.size() - 1) / 2], ys[(ys.size() - 1) / 2]);
        offer(xs[xs.size() / 2], ys[ys.size() / 2]);
        offer(std::clamp(p.source.x, Integer{1}, p.dimensions.x - 1),
              std::clamp(p.source.y, Integer{1}, p.dimensions.y - 1));
        for (std::size_t i = 0; i < std::min(std::size_t{4}, children.size()); ++i) {
            auto const s = tree[children[i]].position; offer(s.x, s.y);
        }
        std::optional<Position> best;
        std::tuple<bool, Integer, Integer, Integer, Integer> best_key;
        for (auto point : points) {
            if (!construction_work || !*construction_work) throw std::runtime_error("construction work budget exceeded");
            --*construction_work;
            Position const s{point.first, point.second};
            if (!free(s)) continue;
            try {
                Integer wire = 0, high = 0, low = std::numeric_limits<Integer>::max();
                for (auto child : children) {
                    if (charge_geometry_edges) {
                        if (!*construction_work) throw std::runtime_error("construction work budget exceeded");
                        --*construction_work;
                    }
                    auto const d = distance(s, tree[child].position);
                    wire = add(wire, d);
                    high = std::max(high, add(d, tree[child].high));
                    low = std::min(low, add(d, tree[child].low));
                }
                auto const key = std::make_tuple(wire > limit, high - low, high, wire, distance(s, p.source));
                if (!best || key < best_key) { best = s; best_key = key; }
            } catch (std::runtime_error const&) {}
        }
        if (!best) throw std::runtime_error("no free multi-output merge position");
        return *best;
    }

    std::size_t merge_multiway(std::vector<std::size_t> children) {
        auto const& type = p.buffer_library[static_cast<std::size_t>(branching_type)];
        auto const s = multiway_position(children, type.length);
        Integer low = std::numeric_limits<Integer>::max(), high = 0;
        for (auto child : children) {
            auto const d = distance(s, tree[child].position);
            low = std::min(low, add(d, tree[child].low)); high = std::max(high, add(d, tree[child].high));
        }
        auto const root = insert(s, branching_type);
        std::sort(children.begin(), children.end(), [&](auto a, auto b) {
            return distance(s, tree[a].position) < distance(s, tree[b].position);
        });
        Integer remaining = type.length;
        for (std::size_t k = 0; k < children.size(); ++k) {
            auto const budget = remaining / static_cast<Integer>(children.size() - k);
            auto const first = route(s, children[k], budget);
            remaining -= distance(s, tree[first].position); tree[root].children.push_back(first);
        }
        tree[root].low = low; tree[root].high = high;
        return root;
    }

    std::size_t cluster_multiway(std::vector<std::size_t> nodes, std::size_t arity_limit = 32) {
        if (nodes.size() == 1) return nodes.front();
        auto const& type = p.buffer_library[static_cast<std::size_t>(branching_type)];
        auto const arity = std::min({arity_limit, nodes.size(),
                                    static_cast<std::size_t>(std::min(type.fanout, type.length))});
        if (arity < 2) throw std::runtime_error("multi-output type cannot branch");
        std::vector<std::size_t> children;
        for (auto group : spatial_groups(std::move(nodes), arity)) children.push_back(cluster_multiway(std::move(group), arity_limit));
        return merge_multiway(std::move(children));
    }

    std::size_t cluster(std::vector<std::size_t> nodes) {
        if (nodes.size() == 1) return nodes.front();
        spatial_sort(nodes);
        auto const middle = nodes.begin() + static_cast<std::ptrdiff_t>(nodes.size() / 2);
        auto const a = cluster({nodes.begin(), middle});
        auto const b = cluster({middle, nodes.end()});
        auto const s = merge_position(a, b);
        auto const low = std::min(add(distance(s, tree[a].position), tree[a].low),
                                  add(distance(s, tree[b].position), tree[b].low));
        auto const high = std::max(add(distance(s, tree[a].position), tree[a].high),
                                   add(distance(s, tree[b].position), tree[b].high));
        auto const root = insert(s, branching_type);
        Integer const limit = p.buffer_library[static_cast<std::size_t>(branching_type)].length;
        Integer const da = distance(s, tree[a].position), db = distance(s, tree[b].position);
        Integer ba = limit / 2, bb = limit - ba;
        if (da < ba) { ba = da; bb = limit - ba; }
        if (db < bb) { bb = db; ba = limit - bb; }
        if (optimize_branch_budget && tree.size() <= 512 && allocation_trials < 128)
            allocate_branch(root, a, b, limit);
        if (tree[root].children.empty()) {
            auto const ca = route(s, a, ba);
            auto const cb = route(s, b, bb);
            tree[root].children = {ca, cb};
        }
        tree[root].low = low; tree[root].high = high;
        return root;
    }

public:
    Builder(Problem const& problem, bool parity, int relay_policy = -1, int branch_policy = -1, bool optimize_allocation = false)
        : p(problem), tree(initial_tree(p, parity)) {
        if (p.dimensions.x <= 1 || p.dimensions.y <= 1)
            throw std::runtime_error("chip has no interior buffer positions");
        // Transformed-coordinate calculations below need signed headroom.
        if (p.dimensions.x > std::numeric_limits<Integer>::max() / 16 ||
            p.dimensions.y > std::numeric_limits<Integer>::max() / 16)
            throw std::runtime_error("chip dimensions exceed construction range");
        for (auto const& n : tree) occupied.emplace(n.position.x, n.position.y);
        for (std::size_t i = 0; i < p.buffer_library.size(); ++i) {
            auto const& type = p.buffer_library[i];
            if (relay_type < 0 || type.length > p.buffer_library[static_cast<std::size_t>(relay_type)].length)
                relay_type = static_cast<int>(i);
            if (type.fanout >= 2 && type.length >= 2 &&
                (branching_type < 0 || type.length > p.buffer_library[static_cast<std::size_t>(branching_type)].length))
                branching_type = static_cast<int>(i);
        }
        if (branch_policy >= 0) branching_type = branch_policy;
        if (relay_policy >= 0) relay_type = relay_policy;
        mixed_relays = relay_policy <= -2;
        shorter_ties = relay_policy == -3;
        optimize_branch_budget = optimize_allocation;
    }
    Tree build() {
        if (branching_type < 0 && p.sinks.size() > 1)
            throw std::runtime_error("buffer library cannot form a branching tree");
        std::vector<std::size_t> leaves;
        for (std::size_t i = 1; i <= p.sinks.size(); ++i) leaves.push_back(i);
        auto const root = cluster(std::move(leaves));
        auto const first = route(p.source, root, p.source_limits.length, true);
        tree[0].children = {first};
        return finish();
    }

    Tree build_multiway(std::size_t source_groups, std::size_t& work) {
        if (branching_type < 0) throw std::runtime_error("no multi-output branch type");
        if (!source_groups || source_groups > p.sinks.size() ||
            source_groups > static_cast<std::uint64_t>(p.source_limits.fanout) ||
            source_groups > static_cast<std::uint64_t>(p.source_limits.length))
            throw std::runtime_error("invalid source group count");
        construction_work = &work;
        std::vector<std::size_t> leaves;
        for (std::size_t i = 1; i <= p.sinks.size(); ++i) leaves.push_back(i);
        std::vector<std::size_t> roots;
        for (auto group : spatial_groups(std::move(leaves), source_groups)) roots.push_back(cluster_multiway(std::move(group)));
        std::sort(roots.begin(), roots.end(), [&](auto a, auto b) {
            return distance(p.source, tree[a].position) < distance(p.source, tree[b].position);
        });
        Integer remaining = p.source_limits.length;
        for (std::size_t k = 0; k < roots.size(); ++k) {
            auto const first = route(p.source, roots[k], remaining / static_cast<Integer>(roots.size() - k), true);
            remaining -= distance(p.source, tree[first].position); tree[0].children.push_back(first);
        }
        return finish();
    }

    Tree build_layered(std::size_t groups, std::size_t& work) {
        if (branching_type < 0 || groups < 2 || groups > p.sinks.size() || groups > 8)
            throw std::runtime_error("invalid layered group count");
        auto const& type = p.buffer_library[static_cast<std::size_t>(branching_type)];
        if (groups > static_cast<std::uint64_t>(std::min(type.fanout, type.length)))
            throw std::runtime_error("layered root cannot drive group count");
        construction_work = &work;
        charge_geometry_edges = true;
        std::vector<std::size_t> leaves;
        for (std::size_t i = 1; i <= p.sinks.size(); ++i) leaves.push_back(i);
        spatial_sort(leaves);
        std::vector<std::size_t> roots;
        for (auto group : proximity_groups(std::move(leaves), groups)) roots.push_back(cluster_multiway(std::move(group), p.sinks.size()));
        auto const root = merge_multiway(std::move(roots));
        tree[0].children = {route(p.source, root, p.source_limits.length, true)};
        return finish();
    }

    Tree build_star() {
        auto const count = static_cast<Integer>(p.sinks.size());
        if (count > p.source_limits.fanout || count > p.source_limits.length)
            throw std::runtime_error("SRC cannot drive one relay chain per sink");
        // Connect near sinks first: they may occupy the only cheap launch
        // point of a longer branch, and their direct edge saves source budget.
        std::vector<std::size_t> order;
        for (std::size_t i = 1; i <= p.sinks.size(); ++i) order.push_back(i);
        std::stable_sort(order.begin(), order.end(), [&](auto a, auto b) {
            return distance(p.source, tree[a].position) < distance(p.source, tree[b].position);
        });
        Integer remaining = p.source_limits.length;
        for (std::size_t k = 0; k < order.size(); ++k) {
            Integer const branches = count - static_cast<Integer>(k);
            Integer const budget = remaining / branches;
            auto const first = route(p.source, order[k], budget, true);
            remaining -= distance(p.source, tree[first].position);
            tree[0].children.push_back(first);
        }
        return finish();
    }

    Tree build_hub() {
        auto const count = static_cast<std::uint64_t>(p.sinks.size());
        if (std::none_of(p.buffer_library.begin(), p.buffer_library.end(),
                         [&](auto const& type) { return static_cast<std::uint64_t>(type.fanout) >= count; }))
            throw std::runtime_error("library cannot drive all sinks from one hub");
        std::vector<Integer> xs, ys;
        for (auto sink : p.sinks) { xs.push_back(sink.x); ys.push_back(sink.y); }
        std::sort(xs.begin(), xs.end()); std::sort(ys.begin(), ys.end());
        std::vector<Position> candidates;
        for (Position base : {Position{xs[xs.size() / 2], ys[ys.size() / 2]},
                              Position{xs.front() + (xs.back() - xs.front()) / 2,
                                       ys.front() + (ys.back() - ys.front()) / 2},
                              Position{std::clamp(p.source.x, Integer{1}, p.dimensions.x - 1),
                                       std::clamp(p.source.y, Integer{1}, p.dimensions.y - 1)}}) {
            for (Integer dx = -2; dx <= 2; ++dx)
                for (Integer dy = -2; dy <= 2; ++dy)
                    candidates.push_back({base.x + dx, base.y + dy});
        }
        std::optional<Tree> best;
        Integer best_score = std::numeric_limits<Integer>::max();
        std::set<std::pair<Integer, Integer>> evaluated;
        for (auto candidate : candidates) {
            if (!free(candidate) || !evaluated.emplace(candidate.x, candidate.y).second) continue;
            try {
                Integer total = 0, low = std::numeric_limits<Integer>::max(), high = 0;
                for (auto sink : p.sinks) {
                    auto const d = distance(candidate, sink);
                    total = add(total, d); low = std::min(low, d); high = std::max(high, d);
                }
                int cheapest = -1;
                for (std::size_t i = 0; i < p.buffer_library.size(); ++i) {
                    auto const& type = p.buffer_library[i];
                    if (static_cast<std::uint64_t>(type.fanout) >= count && type.length >= total &&
                        (cheapest < 0 || type.cost < p.buffer_library[static_cast<std::size_t>(cheapest)].cost))
                        cheapest = static_cast<int>(i);
                }
                if (cheapest < 0) continue;
                auto const skew = high - low;
                if (skew > std::numeric_limits<Integer>::max() / p.skew_weight) continue;
                // All sinks share the source-to-hub path. Routing adds buffer
                // cost but cannot reduce this skew, so this is a lower bound.
                auto const lower_bound = add(p.buffer_library[static_cast<std::size_t>(cheapest)].cost,
                                             skew * p.skew_weight);
                if (best && lower_bound >= best_score) continue;
                // A failed route must not leave occupied positions in later trials.
                Builder trial = *this;
                std::vector<std::size_t> children;
                for (std::size_t i = 1; i <= p.sinks.size(); ++i) children.push_back(i);
                auto const hub = trial.insert(candidate, cheapest, std::move(children));
                auto const first = trial.route(p.source, hub, p.source_limits.length, true);
                trial.tree[0].children = {first};
                auto completed = trial.finish();
                auto const metrics = validate_tree(p, completed);
                if (!best || metrics.score < best_score) {
                    best_score = metrics.score; best = std::move(completed);
                }
            } catch (std::runtime_error const&) {
                // Other placements remain eligible when routing or arithmetic fails.
            }
        }
        if (!best) throw std::runtime_error("no legal multi-output hub tree found");
        return std::move(*best);
    }

private:
    Tree finish() {
        select_buffer_types(p, tree);
        return tree;
    }
};

// Keep the original maximum-length policy, then sample cost/spacing tradeoffs.
// Full-tree comparisons, rather than these ranking heuristics, choose the output.
std::vector<int> buffer_policies(Problem const& p, bool branching = false) {
    std::vector<int> policies{-1};
    std::vector<std::size_t> order;
    Integer longest = 0;
    for (std::size_t i = 0; i < p.buffer_library.size(); ++i) {
        auto const& type = p.buffer_library[i];
        if (branching && (type.fanout < 2 || type.length < 2)) continue;
        order.push_back(i); longest = std::max(longest, type.length);
    }
    if (order.empty()) return policies;
    auto const limit = branching ? 4U : 8U;
    std::set<Integer> lengths{longest};
    auto offer = [&](std::size_t i) {
        if (policies.size() < limit && lengths.insert(p.buffer_library[i].length).second)
            policies.push_back(static_cast<int>(i));
    };
    auto cheapest = order.front(), efficient = order.front();
    auto ratio = [](BufferType const& t) { return static_cast<long double>(t.cost) / t.length; };
    for (auto i : order) {
        auto const& type = p.buffer_library[i];
        auto const& cheap = p.buffer_library[cheapest];
        if (type.cost < cheap.cost || (type.cost == cheap.cost && type.length > cheap.length)) cheapest = i;
        if (ratio(type) < ratio(p.buffer_library[efficient]) ||
            (ratio(type) == ratio(p.buffer_library[efficient]) && type.length > p.buffer_library[efficient].length))
            efficient = i;
    }
    offer(cheapest); offer(efficient);
    std::sort(order.begin(), order.end(), [&](auto i, auto j) {
        auto const& a = p.buffer_library[i]; auto const& b = p.buffer_library[j];
        return a.length != b.length ? a.length > b.length : a.cost < b.cost;
    });
    Integer cheapest_seen = std::numeric_limits<Integer>::max();
    for (auto i : order) {
        auto const cost = p.buffer_library[i].cost;
        if (cost < cheapest_seen) { offer(i); cheapest_seen = cost; }
        if (policies.size() == limit) break;
    }
    if (!branching && lengths.size() > 1) { policies.push_back(-2); policies.push_back(-3); }
    return policies;
}

bool tree_less(Tree const& a, Tree const& b) {
    return std::lexicographical_compare(a.begin(), a.end(), b.begin(), b.end(),
        [](Node const& x, Node const& y) {
            return std::tie(x.position.x, x.position.y, x.type, x.children) <
                   std::tie(y.position.x, y.position.y, y.type, y.children);
        });
}

// Last-resort beam search over small forests. Each merge joins disjoint
// roots through a directly legal buffer; SRC may connect several final groups.
// Existing successful builders never enter this search.
std::optional<Tree> small_forest(Problem const& p, std::size_t& remaining_trials, bool multiway = false,
                                 bool library_arity = false) {
    multiway = multiway || library_arity;
    if (p.sinks.size() > (multiway ? 12U : 8U) || p.buffer_library.size() > 64 ||
        p.dimensions.x <= 1 || p.dimensions.y <= 1 ||
        p.dimensions.x > std::numeric_limits<Integer>::max() / 16 ||
        p.dimensions.y > std::numeric_limits<Integer>::max() / 16) return {};
    Integer max_fanout = 2;
    for (auto const& buf : p.buffer_library) max_fanout = std::max(max_fanout, buf.fanout);
    if (library_arity && max_fanout <= 3) return {};
    struct State { Tree tree; std::vector<std::size_t> roots; Integer priority; };
    State initial{initial_tree(p, false), {}, 0};
    for (std::size_t i = 1; i <= p.sinks.size(); ++i) initial.roots.push_back(i);
    std::vector<State> beam{std::move(initial)};
    auto less = [](State const& a, State const& b) {
        return a.priority != b.priority ? a.priority < b.priority : tree_less(a.tree, b.tree);
    };
    while (!beam.empty() && remaining_trials) {
        std::optional<Tree> complete;
        Integer complete_score = std::numeric_limits<Integer>::max();
        for (auto const& state : beam) {
            --remaining_trials;
            try {
                auto candidate = state.tree;
                candidate[0].children = state.roots;
                auto const score = validate_tree(p, candidate).score;
                if (!complete || score < complete_score) {
                    complete = std::move(candidate); complete_score = score;
                }
            } catch (std::runtime_error const&) {}
            if (!remaining_trials) break;
        }
        if (complete) return complete;
        std::vector<State> next;
        // Reserve work for subsequent layers rather than exhausting the whole
        // search while enumerating one layer of three-root combinations.
        auto layer_trials = multiway ? std::min(std::size_t{4096}, remaining_trials) : remaining_trials;
        for (auto const& state : beam) {
            std::vector<Position> free_positions;
            std::size_t available_positions = 12;
            if (library_arity) {
                Integer occupied_inside = 0;
                for (auto const& n : state.tree)
                    if (n.position.x > 0 && n.position.y > 0 && n.position.x < p.dimensions.x &&
                        n.position.y < p.dimensions.y) ++occupied_inside;
                auto const width = p.dimensions.x - 1, height = p.dimensions.y - 1;
                // Exact capacity for dense chips, safely saturated otherwise.
                if (width <= (occupied_inside + 12) / height)
                    available_positions = static_cast<std::size_t>(width * height - occupied_inside);
                if (!available_positions) continue;
                // Build a bounded, subset-independent pool. Dense cases have
                // few free points, so occupied points do not hide later groups.
                std::set<std::pair<Integer, Integer>> points;
                for (auto const& n : state.tree)
                    for (Integer dx = -1; dx <= 1; ++dx)
                        for (Integer dy = -1; dy <= 1; ++dy) {
                            Position s{n.position.x + dx, n.position.y + dy};
                            if (s.x > 0 && s.y > 0 && s.x < p.dimensions.x && s.y < p.dimensions.y &&
                                std::none_of(state.tree.begin(), state.tree.end(), [&](Node const& other) {
                                    return other.position.x == s.x && other.position.y == s.y;
                                })) points.emplace(s.x, s.y);
                        }
                for (auto point : points) free_positions.push_back({point.first, point.second});
                std::sort(free_positions.begin(), free_positions.end(), [&](Position a, Position b) {
                    return std::make_tuple(distance(p.source, a), a.x, a.y) <
                           std::make_tuple(distance(p.source, b), b.x, b.y);
                });
                if (free_positions.size() > 64) free_positions.resize(64);
                if (free_positions.empty()) continue;
            }
            auto merge = [&](std::vector<std::size_t> const& slots) {
                std::vector<std::size_t> children;
                for (auto slot : slots) children.push_back(state.roots[slot]);
                auto const a = slots[0], b = slots[1];
                auto const ia = state.roots[a], ib = state.roots[b];
                auto const pa = state.tree[ia].position, pb = state.tree[ib].position;
                std::vector<Position> positions = free_positions;
                if (!library_arity) {
                    std::set<std::pair<Integer, Integer>> points;
                    auto offer = [&](Position base) {
                        for (Integer dx = -1; dx <= 1; ++dx)
                            for (Integer dy = -1; dy <= 1; ++dy) {
                                Position s{base.x + dx, base.y + dy};
                                if (s.x > 0 && s.y > 0 && s.x < p.dimensions.x && s.y < p.dimensions.y)
                                    points.emplace(s.x, s.y);
                            }
                    };
                    offer(pa); offer(pb); offer({pa.x, pb.y}); offer({pb.x, pa.y});
                    offer({pa.x + (pb.x - pa.x) / 2, pa.y + (pb.y - pa.y) / 2});
                    offer({std::clamp(p.source.x, Integer{1}, p.dimensions.x - 1),
                           std::clamp(p.source.y, Integer{1}, p.dimensions.y - 1)});
                    if (slots.size() == 3) offer(state.tree[state.roots[slots[2]]].position);
                    for (auto point : points) positions.push_back({point.first, point.second});
                }
                auto wire_length = [&](Position s) {
                    Integer length = 0;
                    for (auto child : children) length = add(length, distance(s, state.tree[child].position));
                    return length;
                };
                std::sort(positions.begin(), positions.end(), [&](Position x, Position y) {
                    auto key = [&](Position s) {
                        return std::make_tuple(wire_length(s),
                                               distance(s, p.source), s.x, s.y);
                    };
                    return key(x) < key(y);
                });
                for (auto s : positions) {
                    if (!layer_trials) break;
                    --remaining_trials; --layer_trials;
                    if (std::any_of(state.tree.begin(), state.tree.end(), [&](Node const& n) {
                        return n.position.x == s.x && n.position.y == s.y;
                    })) continue;
                    try {
                        auto const wire = wire_length(s);
                        int type = -1;
                        for (std::size_t t = 0; t < p.buffer_library.size(); ++t) {
                            auto const& buf = p.buffer_library[t];
                            if (static_cast<std::uint64_t>(buf.fanout) >= children.size() && buf.length >= wire &&
                                (type < 0 || buf.cost < p.buffer_library[static_cast<std::size_t>(type)].cost))
                                type = static_cast<int>(t);
                        }
                        if (type < 0) continue;
                        if (library_arity) {
                            auto const roots = static_cast<Integer>(state.roots.size() - children.size() + 1);
                            auto const excess = std::max(Integer{0}, roots - p.source_limits.fanout);
                            auto const needed = excess / (max_fanout - 1) + (excess % (max_fanout - 1) != 0);
                            if (static_cast<std::uint64_t>(needed) > available_positions - 1) continue;
                        }
                        State trial = state; // Isolate geometry and topology of every attempt.
                        for (auto it = slots.rbegin(); it != slots.rend(); ++it)
                            trial.roots.erase(trial.roots.begin() + static_cast<std::ptrdiff_t>(*it));
                        trial.roots.push_back(trial.tree.size());
                        trial.tree.push_back({s, type, children});
                        trial.priority = 0;
                        for (auto root : trial.roots)
                            trial.priority = add(trial.priority, distance(p.source, trial.tree[root].position));
                        for (auto const& n : trial.tree)
                            if (n.type >= 0) trial.priority = add(trial.priority,
                                p.buffer_library[static_cast<std::size_t>(n.type)].cost);
                        auto it = std::lower_bound(next.begin(), next.end(), trial, less);
                        if (it == next.end() || less(trial, *it)) next.insert(it, std::move(trial));
                        if (next.size() > 64) next.pop_back();
                    } catch (std::runtime_error const&) {}
                }
            };
            if (library_arity) {
                auto const arity = std::min(static_cast<std::size_t>(max_fanout), state.roots.size());
                std::vector<std::size_t> slots;
                auto subsets = [&](auto&& self, std::size_t begin, std::size_t needed) -> void {
                    if (!needed) { merge(slots); return; }
                    for (std::size_t i = begin; i + needed <= state.roots.size() && layer_trials; ++i) {
                        slots.push_back(i); self(self, i + 1, needed - 1); slots.pop_back();
                    }
                };
                for (std::size_t count = arity; count >= 2 && layer_trials; --count) subsets(subsets, 0, count);
            } else {
                for (std::size_t a = 0; a < state.roots.size() && layer_trials; ++a)
                    for (std::size_t b = a + 1; b < state.roots.size() && layer_trials; ++b) {
                        merge({a, b});
                        if (multiway)
                            for (std::size_t c = b + 1; c < state.roots.size() && layer_trials; ++c) merge({a, b, c});
                    }
            }
            if (!layer_trials) break;
        }
        beam = std::move(next);
    }
    return {};
}

Tree solve_reference(Problem const& p) {
    std::optional<Tree> best;
    Integer best_score = std::numeric_limits<Integer>::max();
    std::string reason;
    std::vector<std::pair<Integer, Tree>> shortlist, allocation_shortlist;
    // Two representatives per route family: lowest initial Score and cost.
    // A poor initial skew need not erase a useful star or hub topology.
    std::vector<std::pair<Integer, Tree>> diverse(6, {std::numeric_limits<Integer>::max(), {}});
    auto consider = [&](Tree candidate, bool allocation = false, std::size_t family = 0) {
        auto const metrics = validate_tree(p, candidate);
        if (p.sinks.size() <= 128 && candidate.size() <= 512) {
            auto const cost = metrics.score - p.skew_weight * (metrics.maximum_arrival - metrics.minimum_arrival);
            for (std::size_t kind = 0; kind < 2; ++kind) {
                auto const key = kind == 0 ? metrics.score : cost;
                auto& entry = diverse[2 * family + kind];
                if (entry.second.empty() || key < entry.first ||
                    (key == entry.first && tree_less(candidate, entry.second))) entry = {key, candidate};
            }
        }
        if (candidate.size() <= 512) {
            auto& candidates = allocation ? allocation_shortlist : shortlist;
            auto less = [](auto const& a, auto const& b) {
                return a.first != b.first ? a.first < b.first : tree_less(a.second, b.second);
            };
            std::pair<Integer, Tree> entry{metrics.score, candidate};
            auto it = std::lower_bound(candidates.begin(), candidates.end(), entry, less);
            if (it == candidates.end() || less(entry, *it)) candidates.insert(it, std::move(entry));
            if (candidates.size() > 4) candidates.pop_back();
        }
        if (!allocation && (!best || metrics.score < best_score)) { best_score = metrics.score; best = std::move(candidate); }
    };
    auto direct = initial_tree(p, false);
    for (std::size_t i = 1; i <= p.sinks.size(); ++i) direct[0].children.push_back(i);
    try { consider(std::move(direct)); } catch (std::runtime_error const& e) { reason = e.what(); }
    if (best && best_score == 0) return std::move(*best);
    auto const branches = buffer_policies(p, true);
    for (auto policy : buffer_policies(p)) {
        for (auto branch : branches) {
            for (bool parity : {true, false}) {
                for (bool allocation : {false, true}) {
                    if (allocation && p.sinks.size() > 128) continue;
                    try { consider(Builder(p, parity, policy, branch, allocation).build(), allocation); }
                    catch (std::runtime_error const& e) { reason = e.what(); }
                }
            }
        }
        try { consider(Builder(p, false, policy).build_star(), false, 1); }
        catch (std::runtime_error const& e) { reason = e.what(); }
        try { consider(Builder(p, false, policy).build_hub(), false, 2); }
        catch (std::runtime_error const& e) { reason = e.what(); }
    }
    if (!best && !allocation_shortlist.empty()) best = allocation_shortlist.front().second;
    if (!best) {
        std::size_t remaining_trials = 65536;
        best = small_forest(p, remaining_trials);
        if (!best) {
            remaining_trials = 65536;
            best = small_forest(p, remaining_trials, true);
        }
        if (!best) {
            remaining_trials = 65536;
            best = small_forest(p, remaining_trials, true, true);
        }
    }
    if (!best) throw std::runtime_error("no legal tree constructed: " + reason);
    // Preserve the previous winner's full improvement budget. Alternatives
    // share a separate budget, so a new trial cannot regress its result.
    if (shortlist.empty() && allocation_shortlist.empty())
        return contract_large_tree(p, final_delay(p, refine_tree(p, improve_tree(p, std::move(*best)))));
    auto const original = *best;
    auto improved = improve_tree(p, std::move(*best));
    best_score = validate_tree(p, improved).score;
    std::size_t remaining_trials = 2048;
    auto improve_shortlist = [&](auto& candidates) {
        for (auto& entry : candidates) {
            if (!remaining_trials) break;
            if (!tree_less(entry.second, original) && !tree_less(original, entry.second)) continue;
            auto candidate = improve_tree(p, std::move(entry.second), &remaining_trials);
            auto const score = validate_tree(p, candidate).score;
            if (score < best_score) { best_score = score; improved = std::move(candidate); }
        }
    };
    improve_shortlist(shortlist);
    remaining_trials = 2048;
    improve_shortlist(allocation_shortlist);
    improved = refine_tree(p, std::move(improved));
    best_score = validate_tree(p, improved).score;
    // Preserve the prior winner's entire refinement. Alternatives share an
    // extra budget, with per-candidate slices and a global insertion cap.
    remaining_trials = 32768;
    std::size_t remaining_insertions = 16;
    std::vector<Tree> seen{original};
    for (std::size_t index : {2U, 3U, 4U, 5U, 0U, 1U}) {
        auto& tree = diverse[index].second;
        if (tree.empty() || !remaining_trials) continue;
        if (std::any_of(seen.begin(), seen.end(), [&](Tree const& previous) {
            return !tree_less(tree, previous) && !tree_less(previous, tree);
        })) continue;
        seen.push_back(tree);
        auto slice = std::min(std::size_t{8192}, remaining_trials);
        auto const start = slice;
        auto candidate = improve_tree(p, std::move(tree), &slice);
        candidate = refine_tree(p, std::move(candidate), &slice, &remaining_insertions);
        remaining_trials -= start - slice;
        auto const score = validate_tree(p, candidate).score;
        if (score < best_score) { best_score = score; improved = std::move(candidate); }
    }
    return contract_large_tree(p, final_delay(p, std::move(improved)));
}

// Dense chips can have many pins but only a few possible buffer positions.
// Enumerate those positions instead of fixing spatial sink groups in advance.
std::optional<Tree> dense_forest_search(Problem const& p, std::size_t& work) {
    if (p.sinks.empty() || p.sinks.size() > 128 || p.buffer_library.empty() ||
        p.buffer_library.size() > 64 || p.dimensions.x <= 1 || p.dimensions.y <= 1) return {};
    auto const width = p.dimensions.x - 1, height = p.dimensions.y - 1;
    if (width > 4096 / height) return {};
    std::set<std::pair<Integer, Integer>> occupied;
    occupied.emplace(p.source.x, p.source.y);
    for (auto sink : p.sinks) occupied.emplace(sink.x, sink.y);
    Integer occupied_interior = 0;
    for (auto point : occupied)
        if (point.first > 0 && point.second > 0 && point.first < p.dimensions.x && point.second < p.dimensions.y)
            ++occupied_interior;
    auto const free_count = width * height - occupied_interior;
    if (free_count <= 0 || free_count > 8) return {};
    std::vector<Position> positions;
    for (Integer x = 1; x < p.dimensions.x; ++x)
        for (Integer y = 1; y < p.dimensions.y; ++y)
            if (!occupied.count({x, y})) positions.push_back({x, y});
    auto charge = [&] {
        if (!work) throw std::runtime_error("dense forest work budget exceeded");
        --work;
    };
    auto const sinks = static_cast<Integer>(p.sinks.size());
    Integer buffer_capacity = 0;
    for (auto const& type : p.buffer_library)
        buffer_capacity = std::max(buffer_capacity, std::min({sinks, type.fanout, type.length}));
    auto const masks = std::size_t{1} << positions.size();
    for (std::size_t count = 1; count <= positions.size() && count <= static_cast<std::uint64_t>(p.source_limits.fanout); ++count) {
        for (std::size_t mask = 1; mask < masks; ++mask) {
            charge();
            std::size_t bits = 0;
            for (auto value = mask; value; value >>= 1) bits += value & 1;
            if (bits != count) continue;
            auto tree = initial_tree(p, false);
            std::vector<std::size_t> drivers{0};
            Integer source_wire = 0;
            for (std::size_t i = 0; i < positions.size(); ++i) {
                if (!(mask & (std::size_t{1} << i))) continue;
                auto const node = tree.size();
                tree.push_back({positions[i], 0, {}});
                tree[0].children.push_back(node); drivers.push_back(node);
                source_wire = add(source_wire, distance(p.source, positions[i]));
            }
            if (source_wire > p.source_limits.length) continue;
            auto const direct_capacity = std::min({sinks, p.source_limits.fanout - static_cast<Integer>(count),
                                                   p.source_limits.length - source_wire});
            if (direct_capacity + static_cast<Integer>(count) * buffer_capacity < sinks) continue;
            std::vector<Integer> wire(drivers.size()); wire[0] = source_wire;
            std::vector<std::vector<Integer>> distances(p.sinks.size(), std::vector<Integer>(drivers.size()));
            for (std::size_t sink = 0; sink < p.sinks.size(); ++sink)
                for (std::size_t d = 0; d < drivers.size(); ++d)
                    distances[sink][d] = distance(p.sinks[sink], tree[drivers[d]].position);
            std::vector<bool> assigned(p.sinks.size());
            auto compatible = [&](std::size_t d, Integer length) {
                charge();
                auto const fanout = tree[drivers[d]].children.size() + 1;
                if (!d) return fanout <= static_cast<std::uint64_t>(p.source_limits.fanout) && length <= p.source_limits.length;
                for (auto const& type : p.buffer_library) {
                    charge();
                    if (fanout <= static_cast<std::uint64_t>(type.fanout) && length <= type.length) return true;
                }
                return false;
            };
            std::function<bool(std::size_t)> assign = [&](std::size_t remaining) {
                std::size_t empty = 0;
                for (std::size_t d = 1; d < drivers.size(); ++d) empty += tree[drivers[d]].children.empty();
                if (empty > remaining) return false;
                if (!remaining) {
                    try { select_buffer_types(p, tree); (void)validate_tree(p, tree); return true; }
                    catch (std::runtime_error const&) { return false; }
                }
                std::size_t chosen = 0;
                std::vector<std::size_t> options;
                Integer hardest = -1;
                for (std::size_t sink = 0; sink < p.sinks.size(); ++sink) {
                    if (assigned[sink]) continue;
                    std::vector<std::size_t> eligible;
                    Integer nearest = std::numeric_limits<Integer>::max();
                    for (std::size_t d = 0; d < drivers.size(); ++d) {
                        if (!compatible(d, add(wire[d], distances[sink][d]))) continue;
                        eligible.push_back(d); nearest = std::min(nearest, distances[sink][d]);
                    }
                    if (eligible.empty()) return false;
                    if (options.empty() || eligible.size() < options.size() ||
                        (eligible.size() == options.size() && nearest > hardest)) {
                        chosen = sink; options = std::move(eligible); hardest = nearest;
                    }
                }
                std::sort(options.begin(), options.end(), [&](auto a, auto b) {
                    auto key = [&](auto d) {
                        return std::make_tuple(!(d && tree[drivers[d]].children.empty()), distances[chosen][d], d);
                    };
                    return key(a) < key(b);
                });
                assigned[chosen] = true;
                for (auto d : options) {
                    charge();
                    tree[drivers[d]].children.push_back(chosen + 1); wire[d] += distances[chosen][d];
                    if (assign(remaining - 1)) return true;
                    wire[d] -= distances[chosen][d]; tree[drivers[d]].children.pop_back();
                }
                assigned[chosen] = false;
                return false;
            };
            if (assign(p.sinks.size())) return tree;
        }
    }
    return {};
}

std::optional<Tree> dense_forest(Problem const& p, std::size_t* shared_work = nullptr) {
    auto work = shared_work ? std::min(std::size_t{262144}, *shared_work) : std::size_t{262144};
    auto const start = work;
    std::optional<Tree> result;
    try { result = dense_forest_search(p, work); }
    catch (std::runtime_error const&) {}
    if (shared_work) *shared_work -= start - work;
    return result;
}

std::optional<Integer> score_lower_bound(Problem const& p) {
    if (p.sinks.empty()) return Integer{0};
    // Every edge has integer length >= 1 because coordinates are distinct.
    // N <= effective SRC fanout + sum(buffer fanout - 1), while mixed
    // endpoint parity forces skew >= 1 regardless of intermediate buffers.
    Integer max_fanout = 1, min_cost = std::numeric_limits<Integer>::max();
    for (auto const& type : p.buffer_library) {
        max_fanout = std::max(max_fanout, std::min(type.fanout, type.length));
        min_cost = std::min(min_cost, type.cost);
    }
    auto const excess = std::max(Integer{0}, static_cast<Integer>(p.sinks.size()) -
                                              std::min(p.source_limits.fanout, p.source_limits.length));
    if (excess && max_fanout <= 1) return {};
    auto const buffers = excess ? excess / (max_fanout - 1) + (excess % (max_fanout - 1) != 0) : 0;
    bool mixed_parity = false;
    auto const parity = (p.sinks.front().x % 2 + p.sinks.front().y % 2) % 2;
    for (auto sink : p.sinks) mixed_parity = mixed_parity || (sink.x % 2 + sink.y % 2) % 2 != parity;
    if (buffers && min_cost > std::numeric_limits<Integer>::max() / buffers) return {};
    try { return add(buffers ? buffers * min_cost : 0, mixed_parity ? p.skew_weight : 0); }
    catch (std::runtime_error const&) { return {}; }
}

// Membership and geometry can be jointly necessary: a strict single move
// cannot cross a plateau whose other coordinate/topology change is illegal.
Tree partial_hub_tree(Problem const& p, Tree best, std::size_t* shared_trials = nullptr) {
    if (p.sinks.size() < 2 || p.sinks.size() > 8 || p.buffer_library.size() > 64 ||
        p.dimensions.x <= 1 || p.dimensions.y <= 1 ||
        p.dimensions.x > std::numeric_limits<Integer>::max() / 16 ||
        p.dimensions.y > std::numeric_limits<Integer>::max() / 16) return best;
    auto score = validate_tree(p, best).score;
    auto const lower_bound = score_lower_bound(p);
    auto trials = shared_trials ? std::min(std::size_t{8192}, *shared_trials) : std::size_t{8192};
    auto const start = trials;
    Integer max_capacity = 0;
    for (auto const& type : p.buffer_library) max_capacity = std::max(max_capacity, std::min(type.fanout, type.length));
    if (max_capacity < 2) return best;
    std::set<std::pair<Integer, Integer>> occupied{{p.source.x, p.source.y}};
    for (auto sink : p.sinks) occupied.emplace(sink.x, sink.y);
    auto const full = (std::size_t{1} << p.sinks.size()) - 1;
    // The full hub always receives a slice before partial memberships.
    std::vector<std::size_t> masks{full};
    for (std::size_t mask = 1; mask < full; ++mask) masks.push_back(mask);
    for (auto mask : masks) {
        if (!trials || (lower_bound && score == *lower_bound)) break;
        std::vector<std::size_t> children, direct;
        std::vector<Integer> xs{p.source.x}, ys{p.source.y};
        Integer direct_wire = 0;
        try {
            for (std::size_t i = 0; i < p.sinks.size(); ++i) {
                if (mask & (std::size_t{1} << i)) {
                    children.push_back(i + 1);
                } else {
                    direct.push_back(i + 1); direct_wire = add(direct_wire, distance(p.source, p.sinks[i]));
                }
            }
            if (children.size() < 2 || children.size() > static_cast<std::uint64_t>(max_capacity) ||
                direct.size() + 1 > static_cast<std::uint64_t>(p.source_limits.fanout) ||
                direct_wire >= p.source_limits.length) continue;
            std::vector<Integer> group_x, group_y;
            for (auto child : children) { group_x.push_back(p.sinks[child - 1].x); group_y.push_back(p.sinks[child - 1].y); }
            std::sort(group_x.begin(), group_x.end()); std::sort(group_y.begin(), group_y.end());
            auto coordinates = [](std::vector<Integer> const& values, std::vector<Integer>& anchors) {
                auto const center = values.front() + (values.back() - values.front()) / 2;
                anchors.push_back(values[(values.size() - 1) / 2]); anchors.push_back(values[values.size() / 2]);
                anchors.push_back(center); anchors.push_back(center + (values.back() - values.front()) % 2);
            };
            coordinates(group_x, xs); coordinates(group_y, ys);
            std::set<std::pair<Integer, Integer>> points;
            for (auto x : xs) for (auto y : ys)
                for (Integer dx = -2; dx <= 2; ++dx) for (Integer dy = -2; dy <= 2; ++dy)
                    points.emplace(x + dx, y + dy);
            for (auto point : points) {
                if (!trials) break;
                --trials;
                Position const position{point.first, point.second};
                if (position.x <= 0 || position.y <= 0 || position.x >= p.dimensions.x ||
                    position.y >= p.dimensions.y || occupied.count(point)) continue;
                try {
                    auto const incoming = distance(p.source, position);
                    if (add(direct_wire, incoming) > p.source_limits.length) continue;
                    Integer wire = 0, low = std::numeric_limits<Integer>::max(), high = 0;
                    for (auto leaf : children) {
                        auto const length = distance(position, p.sinks[leaf - 1]); wire = add(wire, length);
                        auto const arrival = add(incoming, length); low = std::min(low, arrival); high = std::max(high, arrival);
                    }
                    for (auto leaf : direct) {
                        auto const arrival = distance(p.source, p.sinks[leaf - 1]);
                        low = std::min(low, arrival); high = std::max(high, arrival);
                    }
                    auto const type = cheapest_buffer_type(p, children.size(), wire);
                    auto const skew = high - low;
                    if (p.skew_weight && skew > std::numeric_limits<Integer>::max() / p.skew_weight) continue;
                    auto const next_score = add(p.buffer_library[static_cast<std::size_t>(type)].cost, skew * p.skew_weight);
                    if (next_score >= score) continue;
                    auto candidate = initial_tree(p, false);
                    candidate[0].children = direct; candidate[0].children.push_back(candidate.size());
                    candidate.push_back({position, type, children});
                    auto const metrics = validate_tree(p, candidate);
                    if (metrics.score < score) { score = metrics.score; best = std::move(candidate); }
                } catch (std::runtime_error const&) {}
            }
        } catch (std::runtime_error const&) {}
    }
    if (shared_trials) *shared_trials -= start - trials;
    return best;
}

Tree joint_bypass_tree(Problem const& p, Tree best, std::size_t* shared_work = nullptr) {
    if (best.size() > 512 || p.sinks.size() > 128) return best;
    auto score = validate_tree(p, best).score;
    auto work = shared_work ? std::min(std::size_t{8192}, *shared_work) : std::size_t{8192};
    auto const start = work;
    auto const trial_limit = local_trial_limit(p, best, nullptr);
    std::size_t trials = 0;
    for (unsigned pass = 0; pass < 8 && work && trials < trial_limit; ++pass) {
        std::optional<Tree> improved;
        auto next_score = score;
        for (std::size_t parent = 0; parent < best.size() && work && trials < trial_limit; ++parent) {
            for (std::size_t slot = 0; slot < best[parent].children.size() && work && trials < trial_limit; ++slot) {
                auto const victim = best[parent].children[slot];
                if (victim <= p.sinks.size() || best[victim].children.size() != 2) continue;
                auto const children = best[victim].children;
                if (children[0] <= p.sinks.size() || children[1] <= p.sinks.size()) continue;
                auto stripped = best;
                auto& edges = stripped[parent].children;
                edges.erase(edges.begin() + static_cast<std::ptrdiff_t>(slot));
                edges.insert(edges.begin() + static_cast<std::ptrdiff_t>(slot), children.begin(), children.end());
                stripped.erase(stripped.begin() + static_cast<std::ptrdiff_t>(victim));
                for (auto& node : stripped) for (auto& child : node.children) if (child > victim) --child;
                auto const a = children[0] - (children[0] > victim), b = children[1] - (children[1] > victim);
                auto const from = parent - (parent > victim);
                std::set<std::pair<Integer, Integer>> occupied;
                for (std::size_t i = 0; i < stripped.size(); ++i)
                    if (i != a && i != b) occupied.emplace(stripped[i].position.x, stripped[i].position.y);
                auto samples = [&](std::size_t node) {
                    auto const origin = stripped[node].position;
                    std::vector<Position> points;
                    auto offer = [&](Position point) {
                        if (!work || points.size() == 32) return;
                        --work;
                        if (point.x <= 0 || point.y <= 0 || point.x >= p.dimensions.x || point.y >= p.dimensions.y ||
                            occupied.count({point.x, point.y})) return;
                        if (std::none_of(points.begin(), points.end(), [&](auto q) { return q.x == point.x && q.y == point.y; }))
                            points.push_back(point);
                    };
                    offer(origin);
                    for (Integer dx = -2; dx <= 2; ++dx) for (Integer dy = -2; dy <= 2; ++dy) {
                        if ((dx > 0 && origin.x > std::numeric_limits<Integer>::max() - dx) ||
                            (dy > 0 && origin.y > std::numeric_limits<Integer>::max() - dy)) continue;
                        offer({origin.x + dx, origin.y + dy});
                    }
                    auto align = [&](Position anchor) {
                        offer({anchor.x, origin.y}); offer({origin.x, anchor.y});
                        offer({origin.x + (anchor.x - origin.x) / 2, origin.y + (anchor.y - origin.y) / 2});
                    };
                    align(stripped[from].position); align(best[victim].position);
                    for (auto child : stripped[node].children) {
                        if (!work || points.size() == 32) break;
                        align(stripped[child].position);
                    }
                    return points;
                };
                auto const first = samples(a), second = samples(b);
                for (auto x : first) {
                    if (!work || trials == trial_limit) break;
                    for (auto y : second) {
                        if (!work || trials == trial_limit) break;
                        --work; ++trials;
                        if (x.x == y.x && x.y == y.y) continue;
                        auto candidate = stripped;
                        candidate[a].position = x; candidate[b].position = y;
                        try {
                            select_buffer_types(p, candidate);
                            auto const metrics = validate_tree(p, candidate);
                            if (metrics.score < next_score) { next_score = metrics.score; improved = std::move(candidate); }
                        } catch (std::runtime_error const&) {}
                    }
                }
            }
        }
        if (!improved) break;
        best = std::move(*improved); score = next_score;
    }
    if (shared_work) *shared_work -= start - work;
    return best;
}

std::vector<int> multiway_policies(Problem const& p) {
    std::vector<std::size_t> eligible;
    for (std::size_t i = 0; i < p.buffer_library.size(); ++i)
        if (p.buffer_library[i].fanout >= 3 && p.buffer_library[i].length >= 3) eligible.push_back(i);
    std::vector<int> policies;
    if (eligible.empty()) return policies;
    auto offer = [&](auto less) {
        auto const chosen = static_cast<int>(*std::min_element(eligible.begin(), eligible.end(), less));
        if (std::find(policies.begin(), policies.end(), chosen) == policies.end()) policies.push_back(chosen);
    };
    auto arity = [&](std::size_t i) {
        auto const& type = p.buffer_library[i]; return std::min({Integer{32}, type.fanout, type.length});
    };
    offer([&](auto a, auto b) { return arity(a) > arity(b); });
    offer([&](auto a, auto b) {
        return static_cast<long double>(p.buffer_library[a].cost) / (arity(a) - 1) <
               static_cast<long double>(p.buffer_library[b].cost) / (arity(b) - 1);
    });
    offer([&](auto a, auto b) { return p.buffer_library[a].cost < p.buffer_library[b].cost; });
    offer([&](auto a, auto b) { return p.buffer_library[a].length > p.buffer_library[b].length; });
    return policies;
}

std::optional<Tree> layered_candidate(Problem const& p) {
    if (p.sinks.size() < 8 || p.sinks.size() > 1024 || p.buffer_library.size() > 64) return {};
    std::size_t work = 262144;
    std::optional<Tree> best;
    auto score = std::numeric_limits<Integer>::max();
    auto const lower_bound = score_lower_bound(p);
    for (auto policy : multiway_policies(p)) for (auto groups : {std::size_t{4}, std::size_t{8}}) {
        if (!work) break;
        try {
            auto tree = Builder(p, true, -1, policy).build_layered(groups, work);
            auto const metrics = validate_tree(p, tree);
            if (!best || metrics.score < score) { score = metrics.score; best = std::move(tree); }
            if (lower_bound && score == *lower_bound) return best;
        } catch (std::runtime_error const&) {}
    }
    return best;
}

Tree solve(Problem const& p) {
    auto const lower_bound = score_lower_bound(p);
    auto optimum = [&](Tree const& tree) {
        return lower_bound && validate_tree(p, tree).score == *lower_bound;
    };
    auto direct = initial_tree(p, false);
    for (std::size_t i = 1; i <= p.sinks.size(); ++i) direct[0].children.push_back(i);
    try { if (optimum(direct)) return direct; } catch (std::runtime_error const&) {}
    auto layered = layered_candidate(p);
    if (layered && optimum(*layered)) return std::move(*layered);

    // First-class fanout-aware construction precedes the binary fallback.
    // Non-optimal candidates get an independent shortlist and cannot displace
    // the complete reference result before their actual refinement is compared.
    std::vector<std::pair<Integer, Tree>> candidates;
    std::size_t work = 1048576, builds = 0;
    if (p.sinks.size() < component_budget && p.buffer_library.size() <= 64) {
        auto const count = std::min({std::size_t{4}, p.sinks.size(),
                                    static_cast<std::size_t>(std::min(p.source_limits.fanout, p.source_limits.length))});
        std::vector<std::size_t> source_groups{count};
        if (count != 1) source_groups.push_back(1);
        for (auto policy : multiway_policies(p))
            for (auto groups : source_groups)
                for (bool parity : {true, false}) {
                    if (!work || builds == 16) continue;
                    ++builds;
                    try {
                        auto tree = Builder(p, parity, -1, policy).build_multiway(groups, work);
                        auto const score = validate_tree(p, tree).score;
                        if (lower_bound && score == *lower_bound) return tree;
                        std::pair<Integer, Tree> entry{score, std::move(tree)};
                        auto less = [](auto const& a, auto const& b) {
                            return a.first != b.first ? a.first < b.first : tree_less(a.second, b.second);
                        };
                        auto it = std::lower_bound(candidates.begin(), candidates.end(), entry, less);
                        if (it == candidates.end() || less(entry, *it)) candidates.insert(it, std::move(entry));
                        if (candidates.size() > 3) candidates.pop_back();
                    } catch (std::runtime_error const&) {}
                }
    }
    std::optional<Tree> best;
    std::string reason;
    try { best = solve_reference(p); }
    catch (std::runtime_error const& e) { reason = e.what(); }
    if (best && optimum(*best)) return std::move(*best);
    auto best_score = best ? validate_tree(p, *best).score : std::numeric_limits<Integer>::max();
    std::size_t candidate_trials = 24576, insertions = 16;
    for (auto& entry : candidates) {
        auto slice = std::min(std::size_t{8192}, candidate_trials);
        auto const start = slice;
        auto tree = adaptive_refine(p, std::move(entry.second), &slice, &insertions);
        tree = final_delay(p, std::move(tree), &slice, &insertions);
        tree = contract_large_tree(p, tree);
        candidate_trials -= start - slice;
        auto const score = validate_tree(p, tree).score;
        if (!best || score < best_score) { best_score = score; best = std::move(tree); }
    }
    if (!best) best = dense_forest(p);
    if (!best && layered) { best = std::move(layered); layered.reset(); }
    if (!best) throw std::runtime_error(reason.empty() ? "no legal tree constructed" : reason);
    std::size_t final_trials = 8192;
    auto tree = adaptive_refine(p, std::move(*best), &final_trials, &insertions);
    tree = final_delay(p, std::move(tree), &final_trials, &insertions);
    tree = partial_hub_tree(p, contract_large_tree(p, tree));
    if (layered) {
        std::size_t trials = 8192, extra_insertions = 16;
        auto candidate = adaptive_refine(p, std::move(*layered), &trials, &extra_insertions);
        candidate = final_delay(p, std::move(candidate), &trials, &extra_insertions);
        candidate = contract_large_tree(p, candidate);
        if (validate_tree(p, candidate).score < validate_tree(p, tree).score) tree = std::move(candidate);
    }
    std::size_t joint_work = 8192, joint_insertions = 16;
    for (unsigned round = 0; round < 3 && joint_work; ++round) {
        auto const score = validate_tree(p, tree).score;
        tree = joint_bypass_tree(p, std::move(tree), &joint_work);
        if (validate_tree(p, tree).score == score) break;
        tree = adaptive_refine(p, std::move(tree), &joint_work, &joint_insertions);
        tree = final_delay(p, std::move(tree), &joint_work, &joint_insertions);
    }
    return tree;
}

std::string serialize_tree(Problem const& p, Tree const& tree) {
    std::vector<std::string> names(tree.size());
    std::size_t buffers = 0, sinks = 0;
    names[0] = "SRC";
    for (std::size_t i = 1; i < tree.size(); ++i)
        names[i] = tree[i].type == -2 ? "S" + std::to_string(++sinks) : "B" + std::to_string(++buffers);
    std::ostringstream output;
    output << ".buffer " << buffers << '\n';
    for (std::size_t i = 1; i < tree.size(); ++i) {
        if (tree[i].type < 0) continue;
        output << names[i] << ' ' << p.buffer_library[static_cast<std::size_t>(tree[i].type)].name
               << ' ' << tree[i].position.x << ' ' << tree[i].position.y << '\n';
    }
    std::vector<std::size_t> queue{0};
    std::vector<std::size_t> depth(tree.size());
    std::size_t max_depth = 0;
    for (std::size_t k = 0; k < queue.size(); ++k) {
        auto const u = queue[k];
        for (auto v : tree[u].children) {
            depth[v] = depth[u] + 1;
            max_depth = std::max(max_depth, depth[v]);
            queue.push_back(v);
        }
    }
    output << ".e\n\n.level " << max_depth << '\n';
    std::size_t current_level = 0;
    for (auto u : queue) {
        if (tree[u].children.empty()) continue;
        auto const level = depth[u] + 1;
        if (level != current_level) {
            if (current_level != 0) output << '\n';
            output << level;
            current_level = level;
        }
        output << ' ' << names[u] << ":{";
        for (std::size_t k = 0; k < tree[u].children.size(); ++k) {
            if (k != 0) output << ' ';
            output << names[tree[u].children[k]];
        }
        output << '}';
    }
    output << "\n.e\n";
    return output.str();
}

} // namespace cbi
