#pragma once

#include "model.hpp"

#include <algorithm>
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

void select_buffer_types(Problem const& p, Tree& tree) {
    for (std::size_t i = p.sinks.size() + 1; i < tree.size(); ++i) {
        Integer length = 0;
        for (auto child : tree[i].children)
            length = add(length, distance(tree[i].position, tree[child].position));
        int cheapest = -1;
        for (std::size_t j = 0; j < p.buffer_library.size(); ++j) {
            auto const& type = p.buffer_library[j];
            if (static_cast<std::uint64_t>(type.fanout) >= tree[i].children.size() && type.length >= length &&
                (cheapest < 0 || type.cost < p.buffer_library[static_cast<std::size_t>(cheapest)].cost))
                cheapest = static_cast<int>(j);
        }
        if (cheapest < 0) throw std::runtime_error("no legal buffer type");
        tree[i].type = cheapest;
    }
}

std::size_t local_trial_limit(Problem const& p, Tree const& tree, std::size_t* remaining_trials) {
    auto const limit = std::min(std::size_t{2048},
        std::size_t{2000000} / tree.size() / (p.buffer_library.size() + 16));
    return remaining_trials ? std::min(limit, *remaining_trials) : limit;
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
        std::set<std::pair<Integer, Integer>> occupied;
        std::vector<std::size_t> parents(best.size());
        for (std::size_t i = 0; i < best.size(); ++i) {
            occupied.emplace(best[i].position.x, best[i].position.y);
            for (auto child : best[i].children) parents[child] = i;
        }
        for (std::size_t i = p.sinks.size() + 1; i < best.size() && trials < trial_limit; ++i) {
            auto const origin = best[i].position;
            std::vector<Position> positions;
            auto offer = [&](Position point) {
                if (positions.size() >= 32 || point.x <= 0 || point.y <= 0 ||
                    point.x >= p.dimensions.x || point.y >= p.dimensions.y ||
                    occupied.count({point.x, point.y})) return;
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
            align(best[parents[i]].position);
            for (auto child : best[i].children) align(best[child].position);
            for (auto point : positions) {
                if (trials == trial_limit) break;
                ++trials;
                if (remaining_trials) --*remaining_trials;
                auto candidate = best;
                candidate[i].position = point;
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
        for (std::size_t a = 1; a <= p.sinks.size() && trials < trial_limit; ++a) {
            for (std::size_t b = a + 1; b <= p.sinks.size() && trials < trial_limit; ++b) {
                if (parents[a] == parents[b]) continue;
                ++trials;
                if (remaining_trials) --*remaining_trials;
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

Tree refine_tree(Problem const& p, Tree best) {
    // Keep the established one-pass result and its budgets as the fallback.
    best = swap_leaves(p, relocate_tree(p, std::move(best)));
    std::size_t remaining_trials = 6144;
    for (unsigned round = 0; round < 3 && remaining_trials; ++round) {
        auto const score = validate_tree(p, best).score;
        best = improve_tree(p, std::move(best), &remaining_trials);
        best = relocate_tree(p, std::move(best), &remaining_trials);
        best = swap_leaves(p, std::move(best), &remaining_trials);
        if (validate_tree(p, best).score == score) break;
    }
    // Preserve the established refinement result before exploring the new
    // membership move, which can unlock earlier geometry/bypass improvements.
    remaining_trials = 8192;
    for (unsigned round = 0; round < 3 && remaining_trials; ++round) {
        auto const score = validate_tree(p, best).score;
        best = reparent_leaves(p, std::move(best), &remaining_trials);
        best = improve_tree(p, std::move(best), &remaining_trials);
        best = relocate_tree(p, std::move(best), &remaining_trials);
        best = swap_leaves(p, std::move(best), &remaining_trials);
        if (validate_tree(p, best).score == score) break;
    }
    remaining_trials = 10240;
    std::size_t remaining_insertions = 16;
    for (unsigned round = 0; round < 3 && remaining_trials; ++round) {
        auto const score = validate_tree(p, best).score;
        best = delay_tree(p, std::move(best), &remaining_trials, &remaining_insertions);
        best = reparent_leaves(p, std::move(best), &remaining_trials);
        best = improve_tree(p, std::move(best), &remaining_trials);
        best = relocate_tree(p, std::move(best), &remaining_trials);
        best = swap_leaves(p, std::move(best), &remaining_trials);
        if (validate_tree(p, best).score == score) break;
    }
    return best;
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

    std::size_t cluster(std::vector<std::size_t> nodes) {
        if (nodes.size() == 1) return nodes.front();
        Integer xmin = p.dimensions.x, xmax = 0, ymin = p.dimensions.y, ymax = 0;
        for (auto n : nodes) {
            auto s = tree[n].position;
            xmin = std::min(xmin, s.x); xmax = std::max(xmax, s.x);
            ymin = std::min(ymin, s.y); ymax = std::max(ymax, s.y);
        }
        bool const xaxis = xmax - xmin >= ymax - ymin;
        std::sort(nodes.begin(), nodes.end(), [&](auto a, auto b) {
            auto pa = tree[a].position, pb = tree[b].position;
            return xaxis ? std::tie(pa.x, pa.y) < std::tie(pb.x, pb.y) :
                           std::tie(pa.y, pa.x) < std::tie(pb.y, pb.x);
        });
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

// Last-resort beam search over small forests. Each merge joins two disjoint
// roots through a directly legal buffer; SRC may connect several final groups.
// Existing successful builders never enter this search.
std::optional<Tree> small_forest(Problem const& p, std::size_t& remaining_trials) {
    if (p.sinks.size() > 8 || p.buffer_library.size() > 64 ||
        p.dimensions.x <= 1 || p.dimensions.y <= 1 ||
        p.dimensions.x > std::numeric_limits<Integer>::max() / 16 ||
        p.dimensions.y > std::numeric_limits<Integer>::max() / 16) return {};
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
        for (auto const& state : beam) {
            for (std::size_t a = 0; a < state.roots.size() && remaining_trials; ++a)
                for (std::size_t b = a + 1; b < state.roots.size() && remaining_trials; ++b) {
                    auto const ia = state.roots[a], ib = state.roots[b];
                    auto const pa = state.tree[ia].position, pb = state.tree[ib].position;
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
                    std::vector<Position> positions;
                    for (auto point : points) positions.push_back({point.first, point.second});
                    std::sort(positions.begin(), positions.end(), [&](Position x, Position y) {
                        auto key = [&](Position s) {
                            return std::make_tuple(add(distance(s, pa), distance(s, pb)),
                                                   distance(s, p.source), s.x, s.y);
                        };
                        return key(x) < key(y);
                    });
                    for (auto s : positions) {
                        if (!remaining_trials) break;
                        --remaining_trials;
                        if (std::any_of(state.tree.begin(), state.tree.end(), [&](Node const& n) {
                            return n.position.x == s.x && n.position.y == s.y;
                        })) continue;
                        try {
                            auto const wire = add(distance(s, pa), distance(s, pb));
                            int type = -1;
                            for (std::size_t t = 0; t < p.buffer_library.size(); ++t) {
                                auto const& buf = p.buffer_library[t];
                                if (buf.fanout >= 2 && buf.length >= wire &&
                                    (type < 0 || buf.cost < p.buffer_library[static_cast<std::size_t>(type)].cost))
                                    type = static_cast<int>(t);
                            }
                            if (type < 0) continue;
                            State trial = state; // Isolate geometry and topology of every attempt.
                            trial.roots.erase(trial.roots.begin() + static_cast<std::ptrdiff_t>(b));
                            trial.roots.erase(trial.roots.begin() + static_cast<std::ptrdiff_t>(a));
                            trial.roots.push_back(trial.tree.size());
                            trial.tree.push_back({s, type, {ia, ib}});
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
                }
            if (!remaining_trials) break;
        }
        beam = std::move(next);
    }
    return {};
}

Tree solve(Problem const& p) {
    std::optional<Tree> best;
    Integer best_score = std::numeric_limits<Integer>::max();
    std::string reason;
    std::vector<std::pair<Integer, Tree>> shortlist, allocation_shortlist;
    auto consider = [&](Tree candidate, bool allocation = false) {
        auto const metrics = validate_tree(p, candidate);
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
        try { consider(Builder(p, false, policy).build_star()); }
        catch (std::runtime_error const& e) { reason = e.what(); }
        try { consider(Builder(p, false, policy).build_hub()); }
        catch (std::runtime_error const& e) { reason = e.what(); }
    }
    if (!best && !allocation_shortlist.empty()) best = allocation_shortlist.front().second;
    if (!best) {
        std::size_t remaining_trials = 65536;
        best = small_forest(p, remaining_trials);
    }
    if (!best) throw std::runtime_error("no legal tree constructed: " + reason);
    // Preserve the previous winner's full improvement budget. Alternatives
    // share a separate budget, so a new trial cannot regress its result.
    if (shortlist.empty() && allocation_shortlist.empty()) return refine_tree(p, improve_tree(p, std::move(*best)));
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
    return refine_tree(p, std::move(improved));
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
