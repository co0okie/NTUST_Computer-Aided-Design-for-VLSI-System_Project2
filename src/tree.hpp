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
        if (tree.size() >= component_budget)
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
            if (tree.size() >= component_budget ||
                static_cast<std::uint64_t>(necessary) > component_budget - tree.size())
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
        auto const ca = route(s, a, ba);
        auto const cb = route(s, b, bb);
        tree[root].children = {ca, cb};
        tree[root].low = low; tree[root].high = high;
        return root;
    }

public:
    Builder(Problem const& problem, bool parity, int relay_policy = -1, int branch_policy = -1)
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
        // Select the cheapest legal type once every outgoing edge is known.
        for (std::size_t i = p.sinks.size() + 1; i < tree.size(); ++i) {
            Integer length = 0;
            for (auto child : tree[i].children) length = add(length, distance(tree[i].position, tree[child].position));
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

Tree solve(Problem const& p) {
    std::optional<Tree> best;
    Integer best_score = std::numeric_limits<Integer>::max();
    std::string reason;
    auto consider = [&](Tree candidate) {
        auto const metrics = validate_tree(p, candidate);
        if (!best || metrics.score < best_score) { best_score = metrics.score; best = std::move(candidate); }
    };
    auto direct = initial_tree(p, false);
    for (std::size_t i = 1; i <= p.sinks.size(); ++i) direct[0].children.push_back(i);
    try { consider(std::move(direct)); } catch (std::runtime_error const& e) { reason = e.what(); }
    if (best && best_score == 0) return std::move(*best);
    auto const branches = buffer_policies(p, true);
    for (auto policy : buffer_policies(p)) {
        for (auto branch : branches) {
            for (bool parity : {true, false}) {
                try { consider(Builder(p, parity, policy, branch).build()); }
                catch (std::runtime_error const& e) { reason = e.what(); }
            }
        }
        try { consider(Builder(p, false, policy).build_star()); }
        catch (std::runtime_error const& e) { reason = e.what(); }
        try { consider(Builder(p, false, policy).build_hub()); }
        catch (std::runtime_error const& e) { reason = e.what(); }
    }
    if (!best) throw std::runtime_error("no legal tree constructed: " + reason);
    return std::move(*best);
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
