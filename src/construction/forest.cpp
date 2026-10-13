#include "construction/forest.hpp"
#include "construction/builder.hpp"
#include "search/local.hpp"
#include "solver/legacy.hpp"
#include "construction/forest.hpp"
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
// Keep the original maximum-length policy, then sample cost/spacing tradeoffs.
// Full-tree comparisons, rather than these ranking heuristics, choose the output.
std::vector<int> buffer_policies(Problem const& p, bool branching) {
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
            return std::make_tuple(x.position.x, x.position.y, x.ordering_key(), x.children) <
                   std::make_tuple(y.position.x, y.position.y, y.ordering_key(), y.children);
        });
}

// Last-resort beam search over small forests. Each merge joins disjoint
// roots through a directly legal buffer; SRC may connect several final groups.
// Existing successful builders never enter this search.
std::optional<Tree> small_forest(Problem const& p, std::size_t& remaining_trials, bool multiway,
                                 bool library_arity) {
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
                        trial.tree.push_back({s, BufferTypeId{static_cast<std::size_t>(type)}, children});
                        trial.priority = 0;
                        for (auto root : trial.roots)
                            trial.priority = add(trial.priority, distance(p.source, trial.tree[root].position));
                        for (auto const& n : trial.tree)
                            if (n.is_buffer()) trial.priority = add(trial.priority,
                                p.buffer_library[n.type_index()].cost);
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

std::optional<Tree> dense_forest_search(Problem const& p, std::size_t& work, bool hierarchical) {
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
    auto assign_sinks = [&](Tree& tree, std::vector<std::size_t> const& drivers, std::vector<Integer> wire) {
        // Skeleton edges already consume fanout and summed wire. Bound the
        // maximum residual capacity before the expensive sink backtracking.
        if (hierarchical) {
            Integer capacity = 0;
            for (std::size_t d = 0; d < drivers.size(); ++d) {
                auto const children = static_cast<Integer>(tree[drivers[d]].children.size());
                Integer residual = 0;
                if (!d) residual = std::min(p.source_limits.fanout - children, p.source_limits.length - wire[d]);
                else for (auto const& type : p.buffer_library) {
                    charge();
                    if (type.fanout >= children && type.length >= wire[d])
                        residual = std::max(residual, std::min(type.fanout - children, type.length - wire[d]));
                }
                capacity += std::min(sinks, residual);
            }
            if (capacity < sinks) return false;
        }
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
        return assign(p.sinks.size());
    };
    auto const masks = std::size_t{1} << positions.size();
    for (std::size_t count = 1; count <= positions.size() && (hierarchical || count <= static_cast<std::uint64_t>(p.source_limits.fanout)); ++count) {
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
                tree.push_back({positions[i], BufferTypeId{0}, {}});
                drivers.push_back(node);
                if (!hierarchical) {
                    tree[0].children.push_back(node);
                    source_wire = add(source_wire, distance(p.source, positions[i]));
                }
            }
            if (source_wire > p.source_limits.length) continue;
            if (!hierarchical) {
                auto const direct_capacity = std::min({sinks, p.source_limits.fanout - static_cast<Integer>(count),
                                                       p.source_limits.length - source_wire});
                if (direct_capacity + static_cast<Integer>(count) * buffer_capacity < sinks) continue;
            } else if (std::min({sinks, p.source_limits.fanout, p.source_limits.length}) +
                       static_cast<Integer>(count) * (buffer_capacity - 1) < sinks) continue;
            std::vector<Integer> wire(drivers.size()); wire[0] = source_wire;
            if (!hierarchical) {
                if (assign_sinks(tree, drivers, wire)) return tree;
                continue;
            }
            std::vector<std::size_t> parents(drivers.size(), drivers.size());
            parents[0] = 0;
            std::function<bool(std::size_t)> skeleton = [&](std::size_t child) {
                if (child == drivers.size()) return assign_sinks(tree, drivers, wire);
                for (std::size_t parent = 0; parent < drivers.size(); ++parent) {
                    charge();
                    if (parent == child) continue;
                    // Every assigned parent chain must be acyclic. Once all
                    // parents are assigned, this guarantees connection to SRC.
                    auto ancestor = parent;
                    while (ancestor && ancestor != child && parents[ancestor] != drivers.size())
                        ancestor = parents[ancestor];
                    if (ancestor == child) continue;
                    auto const edge = distance(tree[drivers[parent]].position, tree[drivers[child]].position);
                    auto const length = add(wire[parent], edge);
                    auto const fanout = tree[drivers[parent]].children.size() + 1;
                    bool fits = false;
                    if (!parent) fits = fanout <= static_cast<std::uint64_t>(p.source_limits.fanout) && length <= p.source_limits.length;
                    else for (auto const& type : p.buffer_library) {
                        charge();
                        if (fanout <= static_cast<std::uint64_t>(type.fanout) && length <= type.length) { fits = true; break; }
                    }
                    if (!fits) continue;
                    parents[child] = parent;
                    tree[drivers[parent]].children.push_back(drivers[child]); wire[parent] = length;
                    if (skeleton(child + 1)) return true;
                    wire[parent] -= edge; tree[drivers[parent]].children.pop_back(); parents[child] = drivers.size();
                }
                return false;
            };
            if (skeleton(1)) return tree;
        }
    }
    return {};
}

std::optional<Tree> dense_forest(Problem const& p, std::size_t* shared_work) {
    auto work = shared_work ? std::min(std::size_t{262144}, *shared_work) : std::size_t{262144};
    auto const start = work;
    std::optional<Tree> result;
    try {
        result = dense_forest_search(p, work);
        if (!result) result = dense_forest_search(p, work, true);
    } catch (std::runtime_error const&) {}
    if (shared_work) *shared_work -= start - work;
    return result;
}


}
