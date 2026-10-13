#include "construction/builder.hpp"
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
class BuilderImpl {
    Problem const& p;
    Tree tree;
    std::set<std::pair<Integer, Integer>> occupied;
    int branching_type = -1;
    int relay_type = -1;
    bool mixed_relays = false;
    bool shorter_ties = false;
    Integer chain_unit = 1;
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
        if (!mixed_relays) return relay_type;
        auto const units = chain_unit == 1 ? remaining : remaining / chain_unit + (remaining % chain_unit != 0);
        if (units > chain_distance_budget) return relay_type;
        auto const extent = static_cast<std::size_t>(units);
        auto const needed = extent + 1 > chain_cost.size() ? extent + 1 - chain_cost.size() : 0;
        if (needed > (chain_transition_budget - chain_transitions) / p.buffer_library.size()) return relay_type;
        while (chain_cost.size() <= extent) {
            Integer const r = static_cast<Integer>(chain_cost.size());
            Integer best = std::numeric_limits<Integer>::max();
            int chosen = -1;
            for (std::size_t i = 0; i < p.buffer_library.size(); ++i) {
                auto const& type = p.buffer_library[i];
                auto const tail = static_cast<std::size_t>(std::max(Integer{0}, r - type.length / chain_unit));
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
        std::optional<BuilderImpl> best;
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
    BuilderImpl(Problem const& problem, bool parity, int relay_policy = -1, int branch_policy = -1, bool optimize_allocation = false)
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
        shorter_ties = relay_policy == -3 || relay_policy == -5;
        if (relay_policy <= -4) {
            chain_unit = 0;
            for (auto const& type : p.buffer_library) chain_unit = std::gcd(chain_unit, type.length);
            construction_limit = 512;
        }
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
                BuilderImpl trial = *this;
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


Builder::Builder(Problem const& p, bool parity, int relay, int branch, bool allocation)
    : impl_(std::make_unique<BuilderImpl>(p, parity, relay, branch, allocation)) {}
Builder::~Builder() = default;
Builder::Builder(Builder&&) noexcept = default;
Builder& Builder::operator=(Builder&&) noexcept = default;
Tree Builder::build() { return impl_->build(); }
Tree Builder::build_star() { return impl_->build_star(); }
Tree Builder::build_hub() { return impl_->build_hub(); }
Tree Builder::build_multiway(std::size_t n, std::size_t& work) { return impl_->build_multiway(n, work); }
Tree Builder::build_layered(std::size_t n, std::size_t& work) { return impl_->build_layered(n, work); }
}
