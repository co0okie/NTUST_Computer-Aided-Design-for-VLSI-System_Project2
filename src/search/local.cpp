#include "search/local.hpp"
#include "construction/builder.hpp"
#include "construction/forest.hpp"
#include "core/control.hpp"
#include "solver/legacy.hpp"
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
class RelocationEvaluationImpl {
    struct Bounds {
        Integer low = std::numeric_limits<Integer>::max(), high = 0;
        bool empty = true;
        void include(Integer minimum, Integer maximum) {
            low = std::min(low, minimum);
            high = std::max(high, maximum);
            empty = false;
        }
        void include(Bounds const &other) {
            if (!other.empty)
                include(other.low, other.high);
        }
    };
    Problem const p;
    Tree const tree;
    std::vector<std::size_t> parents;
    std::vector<Integer> arrivals, wire;
    std::vector<int> types;
    std::vector<Bounds> below, outside;
    std::set<std::pair<Integer, Integer>> occupied;
    Integer cost = 0;

    int driver_type(std::size_t node, Integer length) const {
        if (node != 0)
            return cheapest_buffer_type(p, tree[node].children.size(), length);
        if (tree[0].children.size() > static_cast<std::uint64_t>(p.source_limits.fanout) ||
            length > p.source_limits.length)
            throw std::runtime_error("SRC driving limit exceeded");
        return -1;
    }
    Integer price(int type) const {
        return type < 0 ? 0 : p.buffer_library[static_cast<std::size_t>(type)].cost;
    }

  public:
    RelocationEvaluationImpl(Problem const &problem, Tree const &snapshot)
        : p(problem), tree(snapshot), parents(tree.size()), arrivals(tree.size()),
          wire(tree.size()), types(tree.size(), -1), below(tree.size()), outside(tree.size()) {
        (void)validate_tree(p, tree);
        std::vector<std::size_t> queue{0};
        for (std::size_t k = 0; k < queue.size(); ++k) {
            auto const u = queue[k];
            occupied.emplace(tree[u].position.x, tree[u].position.y);
            for (auto v : tree[u].children) {
                auto const length = distance(tree[u].position, tree[v].position);
                wire[u] = add(wire[u], length);
                arrivals[v] = add(arrivals[u], length);
                parents[v] = u;
                queue.push_back(v);
            }
            if (u > p.sinks.size()) {
                types[u] = driver_type(u, wire[u]);
                cost = add(cost, price(types[u]));
            }
        }
        for (auto it = queue.rbegin(); it != queue.rend(); ++it) {
            auto const u = *it;
            if (u && u <= p.sinks.size())
                below[u].include(arrivals[u], arrivals[u]);
            for (auto v : tree[u].children)
                below[u].include(below[v]);
        }
        for (auto u : queue) {
            auto const &children = tree[u].children;
            std::vector<Bounds> prefix(children.size() + 1);
            for (std::size_t k = 0; k < children.size(); ++k) {
                prefix[k + 1] = prefix[k];
                prefix[k + 1].include(below[children[k]]);
            }
            Bounds suffix;
            for (std::size_t k = children.size(); k > 0; --k) {
                auto const v = children[k - 1];
                outside[v] = outside[u];
                outside[v].include(prefix[k - 1]);
                outside[v].include(suffix);
                suffix.include(below[v]);
            }
        }
    }

    std::size_t parent(std::size_t node) const { return parents[node]; }
    bool occupied_at(Position point) const { return occupied.count({point.x, point.y}) != 0; }

    Metrics evaluate(std::size_t moved, Position point) const {
        if (moved <= p.sinks.size() || moved >= tree.size() || point.x <= 0 || point.y <= 0 ||
            point.x >= p.dimensions.x || point.y >= p.dimensions.y ||
            (occupied_at(point) &&
             (point.x != tree[moved].position.x || point.y != tree[moved].position.y)))
            throw std::runtime_error("invalid relocation");
        auto const from = parents[moved];
        auto const incoming = distance(tree[from].position, point);
        auto const parent_wire =
            add(wire[from] - distance(tree[from].position, tree[moved].position), incoming);
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
        auto const next_cost =
            add(add(cost - price(types[from]) - price(types[moved]), price(parent_type)),
                price(moved_type));
        auto const skew = bounds.high - bounds.low;
        if (p.skew_weight && skew > std::numeric_limits<Integer>::max() / p.skew_weight)
            throw std::runtime_error("score exceeds integer range");
        return {bounds.high, bounds.low, add(next_cost, skew * p.skew_weight)};
    }
};

RelocationEvaluation::RelocationEvaluation(Problem const &p, Tree const &tree)
    : impl_(std::make_unique<RelocationEvaluationImpl>(p, tree)) {}
RelocationEvaluation::~RelocationEvaluation() = default;
std::size_t RelocationEvaluation::parent(std::size_t n) const {
    return impl_->parent(n);
}
bool RelocationEvaluation::occupied_at(Position p) const {
    return impl_->occupied_at(p);
}
Metrics RelocationEvaluation::evaluate(std::size_t n, Position p) const {
    return impl_->evaluate(n, p);
}

std::size_t local_trial_limit(Problem const &p, Tree const &tree, std::size_t *remaining_trials) {
    auto const limit = std::min(std::size_t{2048}, std::size_t{2000000} / tree.size() /
                                                       (p.buffer_library.size() + 16));
    return remaining_trials ? std::min(limit, *remaining_trials) : limit;
}

// Scalable bottom-up contractions for trees excluded from expensive searches.
// Triangle equality preserves every downstream arrival exactly, so a local
// cost reduction cannot trade away skew. Compact and validate only once.
Tree contract_large_tree(Problem const &p, Tree const &original, std::size_t *shared_work) {
    if (original.size() <= 512 || original.size() > component_budget ||
        p.buffer_library.size() > 64)
        return original;
    std::size_t work = shared_work ? std::min(std::size_t{1048576}, *shared_work) : 1048576;
    auto const start = work;
    if (search_expired(p) || !work)
        return original;
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
    for (auto it = order.rbegin(); it != order.rend() && work && !search_expired(p); ++it) {
        auto const parent = *it;
        if (tree[parent].is_sink() || tree[parent].children.size() > 64)
            continue;
        for (std::size_t slot = 0;
             slot < tree[parent].children.size() && work && !search_expired(p);) {
            --work;
            auto const child = tree[parent].children[slot];
            auto const &grandchildren = tree[child].children;
            auto const degree = tree[parent].children.size() - 1 + grandchildren.size();
            if (!tree[child].is_buffer() || degree > 64) {
                ++slot;
                continue;
            }
            try {
                auto const edge = distance(tree[parent].position, tree[child].position);
                Integer length = wire[parent] - edge;
                bool equal = true;
                for (auto grandchild : grandchildren) {
                    if (search_expired(p) || !work) {
                        equal = false;
                        break;
                    }
                    --work;
                    auto const direct = distance(tree[parent].position, tree[grandchild].position);
                    if (direct !=
                        add(edge, distance(tree[child].position, tree[grandchild].position))) {
                        equal = false;
                        break;
                    }
                    length = add(length, direct);
                }
                if (!equal) {
                    ++slot;
                    continue;
                }
                auto const old_cost =
                    parent == 0 ? 0 : p.buffer_library[tree[parent].type_index()].cost;
                auto const saved_cost =
                    add(old_cost, p.buffer_library[tree[child].type_index()].cost);
                int type = -1;
                Integer cost = 0;
                if (parent == 0) {
                    if (degree > static_cast<std::uint64_t>(p.source_limits.fanout) ||
                        length > p.source_limits.length) {
                        ++slot;
                        continue;
                    }
                } else {
                    for (std::size_t t = 0;
                         t < p.buffer_library.size() && work && !search_expired(p); ++t) {
                        --work;
                        auto const &buf = p.buffer_library[t];
                        if (degree <= static_cast<std::uint64_t>(buf.fanout) &&
                            length <= buf.length && (type < 0 || buf.cost < cost)) {
                            type = static_cast<int>(t);
                            cost = buf.cost;
                        }
                    }
                    if (type < 0 || cost >= saved_cost) {
                        ++slot;
                        continue;
                    }
                }
                auto children = tree[parent].children;
                children.erase(children.begin() + static_cast<std::ptrdiff_t>(slot));
                children.insert(children.begin() + static_cast<std::ptrdiff_t>(slot),
                                grandchildren.begin(), grandchildren.end());
                tree[parent].children = std::move(children);
                if (parent != 0)
                    tree[parent].set_buffer_type(BufferTypeId{static_cast<std::size_t>(type)});
                wire[parent] = length;
                removed[child] = true;
                changed = true;
                // Newly exposed grandchildren were already processed bottom-up.
                slot += grandchildren.size();
            } catch (std::runtime_error const &) {
                ++slot;
            }
        }
    }
    if (shared_work)
        *shared_work -= start - work;
    if (!changed)
        return original;
    Tree compact;
    std::vector<std::size_t> mapping(tree.size());
    for (std::size_t i = 0; i < tree.size(); ++i)
        if (!removed[i]) {
            mapping[i] = compact.size();
            compact.push_back(std::move(tree[i]));
        }
    for (auto &n : compact)
        for (auto &child : n.children)
            child = mapping[child];
    try {
        select_buffer_types(p, compact);
        if (validate_tree(p, compact).score < validate_tree(p, original).score)
            return compact;
    } catch (std::runtime_error const &) {
    }
    return original;
}

Tree improve_tree(Problem const &p, Tree best, std::size_t *remaining_trials) {
    // Best-improvement passes prevent an early skew-increasing contraction
    // from hiding a cheaper balanced merge. Bound both tree size and work.
    if (best.size() > 512)
        return best;
    auto score = validate_tree(p, best).score;
    auto const trial_limit = local_trial_limit(p, best, remaining_trials);
    std::size_t trials = 0;
    for (unsigned pass = 0; pass < 16 && trials < trial_limit && !search_expired(p); ++pass) {
        std::optional<Tree> improved;
        auto next_score = score;
        for (std::size_t parent = 0;
             parent < best.size() && trials < trial_limit && !search_expired(p); ++parent) {
            for (std::size_t k = 0;
                 k < best[parent].children.size() && trials < trial_limit && !search_expired(p);
                 ++k) {
                auto const victim = best[parent].children[k];
                if (victim <= p.sinks.size())
                    continue;
                for (bool relocate : {false, true}) {
                    if (relocate && parent == 0)
                        continue;
                    if (search_expired(p) || trials >= trial_limit)
                        break;
                    ++trials;
                    if (remaining_trials)
                        --*remaining_trials;
                    Tree candidate = best;
                    auto const children = candidate[victim].children;
                    auto &edges = candidate[parent].children;
                    edges.erase(edges.begin() + static_cast<std::ptrdiff_t>(k));
                    edges.insert(edges.begin() + static_cast<std::ptrdiff_t>(k), children.begin(),
                                 children.end());
                    if (relocate)
                        candidate[parent].position = candidate[victim].position;
                    candidate.erase(candidate.begin() + static_cast<std::ptrdiff_t>(victim));
                    for (auto &node : candidate)
                        for (auto &child : node.children)
                            if (child > victim)
                                --child;
                    try {
                        select_buffer_types(p, candidate);
                        auto const metrics = validate_tree(p, candidate);
                        if (metrics.score < next_score) {
                            next_score = metrics.score;
                            improved = std::move(candidate);
                        }
                    } catch (std::runtime_error const &) {
                        // Illegal merges leave the original complete tree intact.
                    }
                }
            }
        }
        if (!improved)
            break;
        best = std::move(*improved);
        score = next_score;
    }
    return best;
}

Tree relocate_tree(Problem const &p, Tree best, std::size_t *remaining_trials) {
    if (best.size() > 512)
        return best;
    auto score = validate_tree(p, best).score;
    auto const trial_limit = local_trial_limit(p, best, remaining_trials);
    std::size_t trials = 0;
    for (unsigned pass = 0; pass < 8 && trials < trial_limit && !search_expired(p); ++pass) {
        std::optional<Tree> improved;
        auto next_score = score;
        RelocationEvaluation const evaluation(p, best);
        for (std::size_t i = p.sinks.size() + 1;
             i < best.size() && trials < trial_limit && !search_expired(p); ++i) {
            auto const origin = best[i].position;
            std::vector<Position> positions;
            auto offer = [&](Position point) {
                if (positions.size() >= 32 || point.x <= 0 || point.y <= 0 ||
                    point.x >= p.dimensions.x || point.y >= p.dimensions.y ||
                    evaluation.occupied_at(point))
                    return;
                if (std::none_of(positions.begin(), positions.end(),
                                 [&](auto q) { return q.x == point.x && q.y == point.y; }))
                    positions.push_back(point);
            };
            for (Integer dx = -2; dx <= 2; ++dx)
                for (Integer dy = -2; dy <= 2; ++dy) {
                    if ((dx > 0 && origin.x > std::numeric_limits<Integer>::max() - dx) ||
                        (dy > 0 && origin.y > std::numeric_limits<Integer>::max() - dy))
                        continue;
                    offer({origin.x + dx, origin.y + dy});
                }
            auto align = [&](Position anchor) {
                offer({anchor.x, origin.y});
                offer({origin.x, anchor.y});
                offer({origin.x + (anchor.x - origin.x) / 2, origin.y + (anchor.y - origin.y) / 2});
            };
            align(best[evaluation.parent(i)].position);
            for (auto child : best[i].children)
                align(best[child].position);
            for (auto point : positions) {
                if (search_expired(p) || trials == trial_limit)
                    break;
                ++trials;
                if (remaining_trials)
                    --*remaining_trials;
                try {
                    if (evaluation.evaluate(i, point).score < next_score) {
                        auto candidate = best;
                        candidate[i].position = point;
                        select_buffer_types(p, candidate);
                        auto const metrics = validate_tree(p, candidate);
                        if (metrics.score < next_score) {
                            next_score = metrics.score;
                            improved = std::move(candidate);
                        }
                    }
                } catch (std::runtime_error const &) {
                }
            }
        }
        if (!improved)
            break;
        best = std::move(*improved);
        score = next_score;
    }
    return best;
}

Tree swap_roots(Problem const &p, Tree best, std::size_t *remaining_trials, bool subtrees) {
    if (best.size() > 512 || p.sinks.size() > 128)
        return best;
    auto score = validate_tree(p, best).score;
    auto const trial_limit = local_trial_limit(p, best, remaining_trials);
    std::size_t trials = 0;
    for (unsigned pass = 0; pass < 8 && trials < trial_limit && !search_expired(p); ++pass) {
        std::optional<Tree> improved;
        auto next_score = score;
        std::vector<std::size_t> parents(best.size()), slots(best.size());
        for (std::size_t i = 0; i < best.size(); ++i) {
            for (std::size_t k = 0; k < best[i].children.size(); ++k) {
                auto const child = best[i].children[k];
                parents[child] = i;
                slots[child] = k;
            }
        }
        auto ancestor = [&](std::size_t a, std::size_t b) {
            while (b) {
                if (a == b)
                    return true;
                b = parents[b];
            }
            return false;
        };
        auto const end = subtrees ? best.size() - 1 : p.sinks.size();
        for (std::size_t a = 1; a <= end && trials < trial_limit && !search_expired(p); ++a) {
            for (std::size_t b = a + 1; b <= end && trials < trial_limit && !search_expired(p);
                 ++b) {
                if (parents[a] == parents[b])
                    continue;
                if (subtrees && a <= p.sinks.size() && b <= p.sinks.size())
                    continue;
                ++trials;
                if (remaining_trials)
                    --*remaining_trials;
                if (subtrees && (ancestor(a, b) || ancestor(b, a)))
                    continue;
                auto candidate = best;
                std::swap(candidate[parents[a]].children[slots[a]],
                          candidate[parents[b]].children[slots[b]]);
                try {
                    select_buffer_types(p, candidate);
                    auto const metrics = validate_tree(p, candidate);
                    if (metrics.score < next_score) {
                        next_score = metrics.score;
                        improved = std::move(candidate);
                    }
                } catch (std::runtime_error const &) {
                }
            }
        }
        if (!improved)
            break;
        best = std::move(*improved);
        score = next_score;
    }
    return best;
}

Tree swap_leaves(Problem const &p, Tree best, std::size_t *remaining_trials) {
    return swap_roots(p, std::move(best), remaining_trials, false);
}

Tree swap_subtrees(Problem const &p, Tree best, std::size_t *remaining_trials) {
    return swap_roots(p, std::move(best), remaining_trials, true);
}

Tree reparent_leaves(Problem const &p, Tree best, std::size_t *remaining_trials) {
    if (best.size() > 512 || p.sinks.size() > 128)
        return best;
    auto score = validate_tree(p, best).score;
    auto const trial_limit = local_trial_limit(p, best, remaining_trials);
    auto max_fanout = Integer{0};
    for (auto const &type : p.buffer_library)
        max_fanout = std::max(max_fanout, type.fanout);
    std::size_t trials = 0;
    for (unsigned pass = 0; pass < 8 && trials < trial_limit && !search_expired(p); ++pass) {
        std::optional<Tree> improved;
        auto next_score = score;
        std::vector<std::size_t> parents(best.size()), slots(best.size());
        for (std::size_t i = 0; i < best.size(); ++i) {
            for (std::size_t k = 0; k < best[i].children.size(); ++k) {
                auto const child = best[i].children[k];
                parents[child] = i;
                slots[child] = k;
            }
        }
        for (std::size_t leaf = 1;
             leaf <= p.sinks.size() && trials < trial_limit && !search_expired(p); ++leaf) {
            auto const from = parents[leaf];
            // Keep every existing component connected and every buffer nonempty.
            if (best[from].children.size() == 1)
                continue;
            for (std::size_t target = 0;
                 target < best.size() && trials < trial_limit && !search_expired(p); ++target) {
                if (target == from || best[target].is_sink())
                    continue;
                auto const capacity = target == 0 ? p.source_limits.fanout : max_fanout;
                if (best[target].children.size() >= static_cast<std::uint64_t>(capacity))
                    continue;
                ++trials;
                if (remaining_trials)
                    --*remaining_trials;
                auto candidate = best;
                auto &edges = candidate[from].children;
                edges.erase(edges.begin() + static_cast<std::ptrdiff_t>(slots[leaf]));
                candidate[target].children.push_back(leaf);
                try {
                    select_buffer_types(p, candidate);
                    auto const metrics = validate_tree(p, candidate);
                    if (metrics.score < next_score) {
                        next_score = metrics.score;
                        improved = std::move(candidate);
                    }
                } catch (std::runtime_error const &) {
                }
            }
        }
        if (!improved)
            break;
        best = std::move(*improved);
        score = next_score;
    }
    return best;
}

Tree delay_tree(Problem const &p, Tree best, std::size_t *remaining_trials,
                std::size_t *remaining_insertions) {
    if (best.size() >= 512 || p.sinks.size() > 128 || p.dimensions.x <= 1 || p.dimensions.y <= 1)
        return best;
    auto score = validate_tree(p, best).score;
    auto const trial_limit = local_trial_limit(p, best, remaining_trials);
    std::size_t trials = 0;
    for (unsigned pass = 0; pass < 8 && trials < trial_limit && best.size() < 512; ++pass) {
        if (remaining_insertions && !*remaining_insertions)
            break;
        std::vector<Integer> arrival(best.size()), high(best.size());
        std::vector<std::size_t> queue{0};
        std::set<std::pair<Integer, Integer>> occupied;
        for (std::size_t k = 0; k < queue.size(); ++k) {
            auto const parent = queue[k];
            occupied.emplace(best[parent].position.x, best[parent].position.y);
            for (auto child : best[parent].children) {
                arrival[child] =
                    add(arrival[parent], distance(best[parent].position, best[child].position));
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
        for (std::size_t parent = 0;
             parent < best.size() && trials < trial_limit && !search_expired(p); ++parent) {
            for (std::size_t k = 0;
                 k < best[parent].children.size() && trials < trial_limit && !search_expired(p);
                 ++k) {
                auto const child = best[parent].children[k];
                if (high[child] >= high[0])
                    continue;
                auto const a = best[parent].position, b = best[child].position;
                std::vector<Position> positions;
                auto offer = [&](Position point) {
                    if (positions.size() == 32 || point.x <= 0 || point.y <= 0 ||
                        point.x >= p.dimensions.x || point.y >= p.dimensions.y ||
                        occupied.count({point.x, point.y}))
                        return;
                    if (std::none_of(positions.begin(), positions.end(),
                                     [&](auto q) { return q.x == point.x && q.y == point.y; }))
                        positions.push_back(point);
                };
                for (auto center : {a, b})
                    for (Integer dx = -1; dx <= 1; ++dx)
                        for (Integer dy = -1; dy <= 1; ++dy) {
                            if ((dx > 0 && center.x > std::numeric_limits<Integer>::max() - dx) ||
                                (dy > 0 && center.y > std::numeric_limits<Integer>::max() - dy))
                                continue;
                            offer({center.x + dx, center.y + dy});
                        }
                auto const xmin = std::min(a.x, b.x), xmax = std::max(a.x, b.x);
                auto const ymin = std::min(a.y, b.y), ymax = std::max(a.y, b.y);
                auto const gap = high[0] - high[child];
                for (auto excursion : {gap / 2, gap / 2 + gap % 2}) {
                    for (auto y : {a.y, b.y, ymin + (ymax - ymin) / 2}) {
                        y = std::clamp(y, Integer{1}, p.dimensions.y - 1);
                        if (excursion < xmin)
                            offer({xmin - excursion, y});
                        if (excursion < p.dimensions.x - xmax)
                            offer({xmax + excursion, y});
                    }
                    for (auto x : {a.x, b.x, xmin + (xmax - xmin) / 2}) {
                        x = std::clamp(x, Integer{1}, p.dimensions.x - 1);
                        if (excursion < ymin)
                            offer({x, ymin - excursion});
                        if (excursion < p.dimensions.y - ymax)
                            offer({x, ymax + excursion});
                    }
                }
                for (auto point : positions) {
                    if (search_expired(p) || trials == trial_limit)
                        break;
                    ++trials;
                    if (remaining_trials)
                        --*remaining_trials;
                    try {
                        if (add(distance(a, point), distance(point, b)) <= distance(a, b))
                            continue;
                        auto candidate = best;
                        candidate[parent].children[k] = candidate.size();
                        candidate.push_back({point, BufferTypeId{0}, {child}});
                        select_buffer_types(p, candidate);
                        auto const metrics = validate_tree(p, candidate);
                        if (metrics.score < next_score) {
                            next_score = metrics.score;
                            improved = std::move(candidate);
                        }
                    } catch (std::runtime_error const &) {
                    }
                }
            }
        }
        if (!improved)
            break;
        if (remaining_insertions)
            --*remaining_insertions;
        best = std::move(*improved);
        score = next_score;
    }
    return best;
}

// Revisit delay on the final selected tree, after subtree moves and alternative
// candidate comparison. Its separate cap cannot consume established budgets.
Tree final_delay(Problem const &p, Tree best, std::size_t *shared_trials,
                 std::size_t *shared_insertions) {
    std::size_t trials = shared_trials ? std::min(std::size_t{2048}, *shared_trials) : 2048;
    std::size_t insertions = shared_insertions ? std::min(std::size_t{16}, *shared_insertions) : 16;
    auto const trial_start = trials, insertion_start = insertions;
    best = delay_tree(p, std::move(best), &trials, &insertions);
    if (shared_trials)
        *shared_trials -= trial_start - trials;
    if (shared_insertions)
        *shared_insertions -= insertion_start - insertions;
    return best;
}

enum class Move { bypass, relocate, leaves, reparent, delay, subtrees };

struct SearchBudget {
    std::size_t trials;
    std::size_t insertions;
};

class RefinementScheduler {
    Problem const &p;
    SearchBudget &budget;

    Tree apply(Move move, Tree best, std::size_t &quota, std::size_t slice = 2048) {
        auto allowance = std::min({quota, budget.trials, slice});
        if (search_expired(p) || !allowance)
            return best;
        auto const start = allowance;
        switch (move) {
        case Move::bypass:
            best = improve_tree(p, std::move(best), &allowance);
            break;
        case Move::relocate:
            best = relocate_tree(p, std::move(best), &allowance);
            break;
        case Move::leaves:
            best = swap_leaves(p, std::move(best), &allowance);
            break;
        case Move::reparent:
            best = reparent_leaves(p, std::move(best), &allowance);
            break;
        case Move::delay:
            best = delay_tree(p, std::move(best), &allowance, &budget.insertions);
            break;
        case Move::subtrees:
            best = swap_subtrees(p, std::move(best), &allowance);
            break;
        }
        auto const used = start - allowance;
        budget.trials -= used;
        quota -= used;
        return best;
    }

  public:
    RefinementScheduler(Problem const &problem, SearchBudget &limits)
        : p(problem), budget(limits) {}

    Tree reference(Tree best) {
        // Declarative compatibility schedule preserves every established phase
        // and its result while using the same accounting as adaptive dispatch.
        struct Phase {
            std::size_t trials;
            unsigned rounds;
            std::vector<Move> moves;
        };
        std::vector<Phase> const phases{
            {4096, 1, {Move::relocate, Move::leaves}},
            {6144, 3, {Move::bypass, Move::relocate, Move::leaves}},
            {8192, 3, {Move::reparent, Move::bypass, Move::relocate, Move::leaves}},
            {10240, 3, {Move::delay, Move::reparent, Move::bypass, Move::relocate, Move::leaves}},
            {8192,
             3,
             {Move::subtrees, Move::bypass, Move::relocate, Move::leaves, Move::reparent}}};
        for (auto const &phase : phases) {
            auto quota = std::min(phase.trials, budget.trials);
            for (unsigned round = 0; round < phase.rounds && quota; ++round) {
                auto const score = validate_tree(p, best).score;
                for (auto move : phase.moves)
                    best = apply(move, std::move(best), quota);
                if (validate_tree(p, best).score == score)
                    break;
            }
        }
        return best;
    }

    Tree adaptive(Tree best) {
        if (best.size() > 512 || p.sinks.size() > 128)
            return best;
        std::vector<Move> const moves{Move::bypass,   Move::relocate, Move::leaves,
                                      Move::reparent, Move::subtrees, Move::delay};
        auto pending = moves;
        auto quota = budget.trials;
        auto score = validate_tree(p, best).score;
        for (unsigned dispatch = 0; dispatch < 64 && quota && !pending.empty(); ++dispatch) {
            auto const move = pending.front();
            pending.erase(pending.begin());
            best = apply(move, std::move(best), quota, 512);
            auto const next_score = validate_tree(p, best).score;
            if (next_score < score) {
                score = next_score;
                // Any changed arrival/topology can unlock another operator.
                // Keep at most one pending entry per operator, including itself.
                for (auto retry : moves)
                    if (std::find(pending.begin(), pending.end(), retry) == pending.end())
                        pending.push_back(retry);
            }
        }
        return best;
    }
};

Tree scheduled_refinement(Problem const &p, Tree best, bool adaptive, std::size_t *shared_trials,
                          std::size_t *shared_insertions) {
    auto const cap = adaptive ? std::size_t{8192} : std::size_t{36864};
    SearchBudget budget{shared_trials ? std::min(cap, *shared_trials) : cap,
                        shared_insertions ? std::min(std::size_t{16}, *shared_insertions) : 16};
    auto const initial = budget;
    RefinementScheduler scheduler(p, budget);
    best = adaptive ? scheduler.adaptive(std::move(best)) : scheduler.reference(std::move(best));
    if (shared_trials)
        *shared_trials -= initial.trials - budget.trials;
    if (shared_insertions)
        *shared_insertions -= initial.insertions - budget.insertions;
    return best;
}

Tree refine_tree(Problem const &p, Tree best, std::size_t *trials, std::size_t *insertions) {
    return scheduled_refinement(p, std::move(best), false, trials, insertions);
}

Tree adaptive_refine(Problem const &p, Tree best, std::size_t *trials, std::size_t *insertions) {
    return scheduled_refinement(p, std::move(best), true, trials, insertions);
}

} // namespace cbi
