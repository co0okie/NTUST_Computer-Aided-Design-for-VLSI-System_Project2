#include "solver/legacy.hpp"
#include "construction/builder.hpp"
#include "construction/forest.hpp"
#include "core/control.hpp"
#include "search/local.hpp"
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
Tree solve_reference(Problem const &p) {
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
            auto const cost =
                metrics.score - p.skew_weight * (metrics.maximum_arrival - metrics.minimum_arrival);
            for (std::size_t kind = 0; kind < 2; ++kind) {
                auto const key = kind == 0 ? metrics.score : cost;
                auto &entry = diverse[2 * family + kind];
                if (entry.second.empty() || key < entry.first ||
                    (key == entry.first && tree_less(candidate, entry.second)))
                    entry = {key, candidate};
            }
        }
        if (candidate.size() <= 512) {
            auto &candidates = allocation ? allocation_shortlist : shortlist;
            auto less = [](auto const &a, auto const &b) {
                return a.first != b.first ? a.first < b.first : tree_less(a.second, b.second);
            };
            std::pair<Integer, Tree> entry{metrics.score, candidate};
            auto it = std::lower_bound(candidates.begin(), candidates.end(), entry, less);
            if (it == candidates.end() || less(entry, *it))
                candidates.insert(it, std::move(entry));
            if (candidates.size() > 4)
                candidates.pop_back();
        }
        if (!allocation && (!best || metrics.score < best_score)) {
            best_score = metrics.score;
            best = std::move(candidate);
        }
    };
    auto direct = initial_tree(p, false);
    for (std::size_t i = 1; i <= p.sinks.size(); ++i)
        direct[0].children.push_back(i);
    try {
        consider(std::move(direct));
    } catch (std::runtime_error const &e) {
        reason = e.what();
    }
    if (best && best_score == 0)
        return std::move(*best);
    auto const branches = buffer_policies(p, true);
    for (auto policy : buffer_policies(p)) {
        for (auto branch : branches) {
            for (bool parity : {true, false}) {
                for (bool allocation : {false, true}) {
                    if (allocation && p.sinks.size() > 128)
                        continue;
                    try {
                        consider(Builder(p, parity, policy, branch, allocation).build(),
                                 allocation);
                    } catch (std::runtime_error const &e) {
                        reason = e.what();
                    }
                }
            }
        }
        try {
            consider(Builder(p, false, policy).build_star(), false, 1);
        } catch (std::runtime_error const &e) {
            reason = e.what();
        }
        try {
            consider(Builder(p, false, policy).build_hub(), false, 2);
        } catch (std::runtime_error const &e) {
            reason = e.what();
        }
    }
    if (!best && !allocation_shortlist.empty())
        best = allocation_shortlist.front().second;
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
    if (!best)
        throw std::runtime_error("no legal tree constructed: " + reason);
    // Preserve the previous winner's full improvement budget. Alternatives
    // share a separate budget, so a new trial cannot regress its result.
    if (shortlist.empty() && allocation_shortlist.empty())
        return contract_large_tree(
            p, refine_tree(p, improve_tree(p, std::move(*best))));
    auto const original = *best;
    auto improved = improve_tree(p, std::move(*best));
    best_score = validate_tree(p, improved).score;
    std::size_t remaining_trials = 2048;
    auto improve_shortlist = [&](auto &candidates) {
        for (auto &entry : candidates) {
            if (search_expired(p) || !remaining_trials)
                break;
            if (!tree_less(entry.second, original) && !tree_less(original, entry.second))
                continue;
            auto candidate = improve_tree(p, std::move(entry.second), &remaining_trials);
            auto const score = validate_tree(p, candidate).score;
            if (score < best_score) {
                best_score = score;
                improved = std::move(candidate);
            }
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
        auto &tree = diverse[index].second;
        if (tree.empty() || !remaining_trials)
            continue;
        if (std::any_of(seen.begin(), seen.end(), [&](Tree const &previous) {
                return !tree_less(tree, previous) && !tree_less(previous, tree);
            }))
            continue;
        seen.push_back(tree);
        auto slice = std::min(std::size_t{8192}, remaining_trials);
        auto const start = slice;
        auto candidate = improve_tree(p, std::move(tree), &slice);
        candidate = refine_tree(p, std::move(candidate), &slice, &remaining_insertions);
        remaining_trials -= start - slice;
        auto const score = validate_tree(p, candidate).score;
        if (score < best_score) {
            best_score = score;
            improved = std::move(candidate);
        }
    }
    return contract_large_tree(p, improved);
}

// Dense chips can have many pins but only a few possible buffer positions.
// Enumerate those positions instead of fixing spatial sink groups in advance.
std::optional<Integer> score_lower_bound(Problem const &p) {
    if (p.sinks.empty())
        return Integer{0};
    // Every edge has integer length >= 1 because coordinates are distinct.
    // N <= effective SRC fanout + sum(buffer fanout - 1), while mixed
    // endpoint parity forces skew >= 1 regardless of intermediate buffers.
    Integer max_fanout = 1, min_cost = std::numeric_limits<Integer>::max();
    for (auto const &type : p.buffer_library) {
        max_fanout = std::max(max_fanout, std::min(type.fanout, type.length));
        min_cost = std::min(min_cost, type.cost);
    }
    auto const excess =
        std::max(Integer{0}, static_cast<Integer>(p.sinks.size()) -
                                 std::min(p.source_limits.fanout, p.source_limits.length));
    if (excess && max_fanout <= 1)
        return {};
    auto buffers = excess ? excess / (max_fanout - 1) + (excess % (max_fanout - 1) != 0) : 0;
    bool direct_possible = p.sinks.size() <= static_cast<std::uint64_t>(p.source_limits.fanout);
    Integer direct_wire = 0;
    for (auto sink : p.sinks) {
        if (!direct_possible)
            break;
        try {
            auto const length = distance(p.source, sink);
            if (length > p.source_limits.length - direct_wire)
                direct_possible = false;
            else
                direct_wire += length;
        } catch (std::runtime_error const &) {
            direct_possible = false;
        }
    }
    if (!direct_possible)
        buffers = std::max(buffers, Integer{1});
    bool mixed_parity = false;
    auto const parity = (p.sinks.front().x % 2 + p.sinks.front().y % 2) % 2;
    for (auto sink : p.sinks)
        mixed_parity = mixed_parity || (sink.x % 2 + sink.y % 2) % 2 != parity;
    if (buffers && min_cost > std::numeric_limits<Integer>::max() / buffers)
        return {};
    try {
        return add(buffers ? buffers * min_cost : 0, mixed_parity ? p.skew_weight : 0);
    } catch (std::runtime_error const &) {
        return {};
    }
}

// Membership and geometry can be jointly necessary: a strict single move
// cannot cross a plateau whose other coordinate/topology change is illegal.
Tree partial_hub_tree(Problem const &p, Tree best, std::size_t *shared_trials,
                      bool prioritize_deficits) {
    if (p.sinks.size() < 2 || p.sinks.size() > 8 || p.buffer_library.size() > 64 ||
        p.dimensions.x <= 1 || p.dimensions.y <= 1 ||
        p.dimensions.x > std::numeric_limits<Integer>::max() / 16 ||
        p.dimensions.y > std::numeric_limits<Integer>::max() / 16)
        return best;
    auto score = validate_tree(p, best).score;
    auto const lower_bound = score_lower_bound(p);
    auto trials = shared_trials ? std::min(std::size_t{8192}, *shared_trials) : std::size_t{8192};
    auto const start = trials;
    Integer max_capacity = 0;
    for (auto const &type : p.buffer_library)
        max_capacity = std::max(max_capacity, std::min(type.fanout, type.length));
    if (max_capacity < 2)
        return best;
    std::set<std::pair<Integer, Integer>> occupied{{p.source.x, p.source.y}};
    for (auto sink : p.sinks)
        occupied.emplace(sink.x, sink.y);
    auto const full = (std::size_t{1} << p.sinks.size()) - 1;
    // The full hub always receives a slice before partial memberships.
    std::vector<std::size_t> masks{full};
    for (std::size_t mask = 1; mask < full; ++mask)
        masks.push_back(mask);
    if (prioritize_deficits) {
        std::vector<Integer> arrivals;
        Integer latest = 0;
        for (auto sink : p.sinks) {
            arrivals.push_back(distance(p.source, sink));
            latest = std::max(latest, arrivals.back());
        }
        struct Group {
            std::size_t mask;
            Integer deficit = 0;
            std::vector<std::pair<Integer, Integer>> coordinates;
        };
        std::vector<Group> groups;
        for (std::size_t mask = 1; mask < full; ++mask) {
            Group group{mask, 0, {}};
            for (std::size_t i = 0; i < p.sinks.size(); ++i)
                if (mask & (std::size_t{1} << i)) {
                    group.deficit = add(group.deficit, latest - arrivals[i]);
                    group.coordinates.emplace_back(p.sinks[i].x, p.sinks[i].y);
                }
            std::sort(group.coordinates.begin(), group.coordinates.end());
            groups.push_back(std::move(group));
        }
        std::sort(groups.begin(), groups.end(), [](Group const &a, Group const &b) {
            if (a.deficit != b.deficit)
                return a.deficit > b.deficit;
            if (a.coordinates.size() != b.coordinates.size())
                return a.coordinates.size() < b.coordinates.size();
            return a.coordinates < b.coordinates;
        });
        masks = {full};
        for (auto const &group : groups)
            masks.push_back(group.mask);
    }
    for (auto mask : masks) {
        if (search_expired(p) || !trials || (lower_bound && score == *lower_bound))
            break;
        std::vector<std::size_t> children, direct;
        std::vector<Integer> xs{p.source.x}, ys{p.source.y};
        Integer direct_wire = 0;
        try {
            for (std::size_t i = 0; i < p.sinks.size(); ++i) {
                if (mask & (std::size_t{1} << i)) {
                    children.push_back(i + 1);
                } else {
                    direct.push_back(i + 1);
                    direct_wire = add(direct_wire, distance(p.source, p.sinks[i]));
                }
            }
            if (children.size() < 2 || children.size() > static_cast<std::uint64_t>(max_capacity) ||
                direct.size() + 1 > static_cast<std::uint64_t>(p.source_limits.fanout) ||
                direct_wire >= p.source_limits.length)
                continue;
            std::vector<Integer> group_x, group_y;
            for (auto child : children) {
                group_x.push_back(p.sinks[child - 1].x);
                group_y.push_back(p.sinks[child - 1].y);
            }
            std::sort(group_x.begin(), group_x.end());
            std::sort(group_y.begin(), group_y.end());
            auto coordinates = [](std::vector<Integer> const &values,
                                  std::vector<Integer> &anchors) {
                auto const center = values.front() + (values.back() - values.front()) / 2;
                anchors.push_back(values[(values.size() - 1) / 2]);
                anchors.push_back(values[values.size() / 2]);
                anchors.push_back(center);
                anchors.push_back(center + (values.back() - values.front()) % 2);
            };
            coordinates(group_x, xs);
            coordinates(group_y, ys);
            std::set<std::pair<Integer, Integer>> points;
            for (auto x : xs)
                for (auto y : ys)
                    for (Integer dx = -2; dx <= 2; ++dx)
                        for (Integer dy = -2; dy <= 2; ++dy)
                            points.emplace(x + dx, y + dy);
            for (auto point : points) {
                if (search_expired(p) || !trials || (lower_bound && score == *lower_bound))
                    break;
                --trials;
                Position const position{point.first, point.second};
                if (position.x <= 0 || position.y <= 0 || position.x >= p.dimensions.x ||
                    position.y >= p.dimensions.y || occupied.count(point))
                    continue;
                try {
                    auto const incoming = distance(p.source, position);
                    if (add(direct_wire, incoming) > p.source_limits.length)
                        continue;
                    Integer wire = 0, low = std::numeric_limits<Integer>::max(), high = 0;
                    for (auto leaf : children) {
                        auto const length = distance(position, p.sinks[leaf - 1]);
                        wire = add(wire, length);
                        auto const arrival = add(incoming, length);
                        low = std::min(low, arrival);
                        high = std::max(high, arrival);
                    }
                    for (auto leaf : direct) {
                        auto const arrival = distance(p.source, p.sinks[leaf - 1]);
                        low = std::min(low, arrival);
                        high = std::max(high, arrival);
                    }
                    auto const type = cheapest_buffer_type(p, children.size(), wire);
                    auto const skew = high - low;
                    if (p.skew_weight && skew > std::numeric_limits<Integer>::max() / p.skew_weight)
                        continue;
                    auto const next_score =
                        add(p.buffer_library[static_cast<std::size_t>(type)].cost,
                            skew * p.skew_weight);
                    if (next_score >= score)
                        continue;
                    auto candidate = initial_tree(p, false);
                    candidate[0].children = direct;
                    candidate[0].children.push_back(candidate.size());
                    candidate.push_back(
                        {position, BufferTypeId{static_cast<std::size_t>(type)}, children});
                    auto const metrics = validate_tree(p, candidate);
                    if (metrics.score < score) {
                        score = metrics.score;
                        best = std::move(candidate);
                    }
                } catch (std::runtime_error const &) {
                }
            }
        } catch (std::runtime_error const &) {
        }
    }
    if (shared_trials)
        *shared_trials -= start - trials;
    return best;
}

Tree joint_bypass_tree(Problem const &p, Tree best, std::size_t *shared_work) {
    if (best.size() > 512 || p.sinks.size() > 128)
        return best;
    auto score = validate_tree(p, best).score;
    auto work = shared_work ? std::min(std::size_t{8192}, *shared_work) : std::size_t{8192};
    auto const start = work;
    auto const trial_limit = local_trial_limit(p, best, nullptr);
    std::size_t trials = 0;
    for (unsigned pass = 0; pass < 8 && work && trials < trial_limit && !search_expired(p);
         ++pass) {
        std::optional<Tree> improved;
        auto next_score = score;
        for (std::size_t parent = 0;
             parent < best.size() && work && trials < trial_limit && !search_expired(p); ++parent) {
            for (std::size_t slot = 0; slot < best[parent].children.size() && work &&
                                       trials < trial_limit && !search_expired(p);
                 ++slot) {
                auto const victim = best[parent].children[slot];
                if (victim <= p.sinks.size() || best[victim].children.size() != 2)
                    continue;
                auto const children = best[victim].children;
                if (children[0] <= p.sinks.size() || children[1] <= p.sinks.size())
                    continue;
                auto stripped = best;
                auto &edges = stripped[parent].children;
                edges.erase(edges.begin() + static_cast<std::ptrdiff_t>(slot));
                edges.insert(edges.begin() + static_cast<std::ptrdiff_t>(slot), children.begin(),
                             children.end());
                stripped.erase(stripped.begin() + static_cast<std::ptrdiff_t>(victim));
                for (auto &node : stripped)
                    for (auto &child : node.children)
                        if (child > victim)
                            --child;
                auto const a = children[0] - (children[0] > victim),
                           b = children[1] - (children[1] > victim);
                auto const from = parent - (parent > victim);
                std::set<std::pair<Integer, Integer>> occupied;
                for (std::size_t i = 0; i < stripped.size(); ++i)
                    if (i != a && i != b)
                        occupied.emplace(stripped[i].position.x, stripped[i].position.y);
                auto samples = [&](std::size_t node) {
                    auto const origin = stripped[node].position;
                    std::vector<Position> points;
                    auto offer = [&](Position point) {
                        if (search_expired(p) || !work || points.size() == 32)
                            return;
                        --work;
                        if (point.x <= 0 || point.y <= 0 || point.x >= p.dimensions.x ||
                            point.y >= p.dimensions.y || occupied.count({point.x, point.y}))
                            return;
                        if (std::none_of(points.begin(), points.end(),
                                         [&](auto q) { return q.x == point.x && q.y == point.y; }))
                            points.push_back(point);
                    };
                    offer(origin);
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
                        offer({origin.x + (anchor.x - origin.x) / 2,
                               origin.y + (anchor.y - origin.y) / 2});
                    };
                    align(stripped[from].position);
                    align(best[victim].position);
                    for (auto child : stripped[node].children) {
                        if (search_expired(p) || !work || points.size() == 32)
                            break;
                        align(stripped[child].position);
                    }
                    return points;
                };
                auto const first = samples(a), second = samples(b);
                for (auto x : first) {
                    if (search_expired(p) || !work || trials == trial_limit)
                        break;
                    for (auto y : second) {
                        if (search_expired(p) || !work || trials == trial_limit)
                            break;
                        --work;
                        ++trials;
                        if (x.x == y.x && x.y == y.y)
                            continue;
                        auto candidate = stripped;
                        candidate[a].position = x;
                        candidate[b].position = y;
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
            }
        }
        if (!improved)
            break;
        best = std::move(*improved);
        score = next_score;
    }
    if (shared_work)
        *shared_work -= start - work;
    return best;
}

std::vector<int> multiway_policies(Problem const &p) {
    std::vector<std::size_t> eligible;
    for (std::size_t i = 0; i < p.buffer_library.size(); ++i)
        if (p.buffer_library[i].fanout >= 3 && p.buffer_library[i].length >= 3)
            eligible.push_back(i);
    std::vector<int> policies;
    if (eligible.empty())
        return policies;
    auto offer = [&](auto less) {
        auto const chosen =
            static_cast<int>(*std::min_element(eligible.begin(), eligible.end(), less));
        if (std::find(policies.begin(), policies.end(), chosen) == policies.end())
            policies.push_back(chosen);
    };
    auto arity = [&](std::size_t i) {
        auto const &type = p.buffer_library[i];
        return std::min({Integer{32}, type.fanout, type.length});
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

std::optional<Tree> layered_candidate(Problem const &p) {
    if (p.sinks.size() < 8 || p.sinks.size() > 1024 || p.buffer_library.size() > 64)
        return {};
    std::size_t work = 262144;
    std::optional<Tree> best;
    auto score = std::numeric_limits<Integer>::max();
    auto const lower_bound = score_lower_bound(p);
    for (auto policy : multiway_policies(p))
        for (auto groups : {std::size_t{4}, std::size_t{8}}) {
            if (search_expired(p) || !work)
                break;
            try {
                auto tree = Builder(p, true, -1, policy).build_layered(groups, work);
                auto const metrics = validate_tree(p, tree);
                if (!best || metrics.score < score) {
                    score = metrics.score;
                    best = std::move(tree);
                }
                if (lower_bound && score == *lower_bound)
                    return best;
            } catch (std::runtime_error const &) {
            }
        }
    return best;
}

std::optional<Tree> compressed_chain_candidate(Problem const &p) {
    if (p.sinks.empty() || p.sinks.size() > 128 || p.buffer_library.size() < 2 ||
        p.buffer_library.size() > 64)
        return {};
    Integer unit = 0;
    std::set<Integer> lengths;
    for (auto const &type : p.buffer_library) {
        unit = std::gcd(unit, type.length);
        lengths.insert(type.length);
    }
    if (unit <= 1 || lengths.size() < 2)
        return {};
    std::optional<Tree> best;
    Integer score = std::numeric_limits<Integer>::max();
    auto consider = [&](Tree tree) {
        auto const metrics = validate_tree(p, tree);
        if (!best || metrics.score < score) {
            score = metrics.score;
            best = std::move(tree);
        }
    };
    for (int policy : {-4, -5}) {
        for (bool parity : {true, false}) {
            try {
                consider(Builder(p, parity, policy).build());
            } catch (std::runtime_error const &) {
            }
        }
        try {
            consider(Builder(p, false, policy).build_star());
        } catch (std::runtime_error const &) {
        }
        try {
            consider(Builder(p, false, policy).build_hub());
        } catch (std::runtime_error const &) {
        }
    }
    return best;
}

Tree solve_core(Problem const &p) {
    auto const lower_bound = score_lower_bound(p);
    auto optimum = [&](Tree const &tree) {
        return lower_bound && validate_tree(p, tree).score == *lower_bound;
    };
    auto direct = initial_tree(p, false);
    for (std::size_t i = 1; i <= p.sinks.size(); ++i)
        direct[0].children.push_back(i);
    try {
        if (optimum(direct))
            return direct;
    } catch (std::runtime_error const &) {
    }
    auto layered = layered_candidate(p);
    if (layered && optimum(*layered))
        return std::move(*layered);
    auto compressed = compressed_chain_candidate(p);
    if (compressed && optimum(*compressed))
        return std::move(*compressed);

    // First-class fanout-aware construction precedes the binary fallback.
    // Non-optimal candidates get an independent shortlist and cannot displace
    // the complete reference result before their actual refinement is compared.
    std::vector<std::pair<Integer, Tree>> candidates;
    std::size_t work = 1048576, builds = 0;
    if (p.sinks.size() < component_budget && p.buffer_library.size() <= 64) {
        auto const count = std::min(
            {std::size_t{4}, p.sinks.size(),
             static_cast<std::size_t>(std::min(p.source_limits.fanout, p.source_limits.length))});
        std::vector<std::size_t> source_groups{count};
        if (count != 1)
            source_groups.push_back(1);
        for (auto policy : multiway_policies(p))
            for (auto groups : source_groups)
                for (bool parity : {true, false}) {
                    if (search_expired(p) || !work || builds == 16)
                        continue;
                    ++builds;
                    try {
                        auto tree = Builder(p, parity, -1, policy).build_multiway(groups, work);
                        auto const score = validate_tree(p, tree).score;
                        if (lower_bound && score == *lower_bound)
                            return tree;
                        std::pair<Integer, Tree> entry{score, std::move(tree)};
                        auto less = [](auto const &a, auto const &b) {
                            return a.first != b.first ? a.first < b.first
                                                      : tree_less(a.second, b.second);
                        };
                        auto it =
                            std::lower_bound(candidates.begin(), candidates.end(), entry, less);
                        if (it == candidates.end() || less(entry, *it))
                            candidates.insert(it, std::move(entry));
                        if (candidates.size() > 3)
                            candidates.pop_back();
                    } catch (std::runtime_error const &) {
                    }
                }
    }
    std::optional<Tree> best;
    std::string reason;
    try {
        best = solve_reference(p);
    } catch (std::runtime_error const &e) {
        reason = e.what();
    }
    if (best && optimum(*best))
        return std::move(*best);
    auto best_score = best ? validate_tree(p, *best).score : std::numeric_limits<Integer>::max();
    std::size_t candidate_trials = 24576, insertions = 16;
    for (auto &entry : candidates) {
        auto slice = std::min(std::size_t{8192}, candidate_trials);
        auto const start = slice;
        auto tree = adaptive_refine(p, std::move(entry.second), &slice, &insertions);
        tree = contract_large_tree(p, tree);
        candidate_trials -= start - slice;
        auto const score = validate_tree(p, tree).score;
        if (!best || score < best_score) {
            best_score = score;
            best = std::move(tree);
        }
    }
    if (!best)
        best = dense_forest(p);
    if (!best && layered) {
        best = std::move(layered);
        layered.reset();
    }
    if (!best && compressed) {
        best = std::move(compressed);
        compressed.reset();
    }
    if (!best)
        throw std::runtime_error(reason.empty() ? "no legal tree constructed" : reason);
    std::size_t final_trials = 8192;
    auto tree = adaptive_refine(p, std::move(*best), &final_trials, &insertions);
    tree = partial_hub_tree(p, contract_large_tree(p, tree));
    for (auto extra : {&layered, &compressed}) {
        if (!*extra)
            continue;
        std::size_t trials = 8192, extra_insertions = 16;
        auto candidate = adaptive_refine(p, std::move(**extra), &trials, &extra_insertions);
        candidate = contract_large_tree(p, candidate);
        if (validate_tree(p, candidate).score < validate_tree(p, tree).score)
            tree = std::move(candidate);
    }
    std::size_t joint_work = 8192, joint_insertions = 16;
    for (unsigned round = 0; round < 3 && joint_work; ++round) {
        auto const score = validate_tree(p, tree).score;
        tree = joint_bypass_tree(p, std::move(tree), &joint_work);
        if (validate_tree(p, tree).score == score)
            break;
        tree = adaptive_refine(p, std::move(tree), &joint_work, &joint_insertions);
    }
    return partial_hub_tree(p, std::move(tree), &joint_work, true);
}

// Large unused dimensions need not force large arithmetic in a compact pin
// cluster. Translation preserves every Manhattan edge; clipping supplies only
// an additional candidate, never removes the original solver's result.
std::optional<std::pair<Problem, Position>> local_domain(Problem const &p) {
    auto const limit = std::numeric_limits<Integer>::max() / 16;
    if (p.dimensions.x <= limit && p.dimensions.y <= limit)
        return {};
    Position low = p.source, high = p.source;
    for (auto sink : p.sinks) {
        low.x = std::min(low.x, sink.x);
        low.y = std::min(low.y, sink.y);
        high.x = std::max(high.x, sink.x);
        high.y = std::max(high.y, sink.y);
    }
    Position const origin{low.x > 64 ? low.x - 64 : 0, low.y > 64 ? low.y - 64 : 0};
    auto const xmax = high.x - origin.x, ymax = high.y - origin.y;
    if (xmax > limit - 64 || ymax > limit - 64)
        return {};
    auto local = p;
    local.dimensions = {std::min(p.dimensions.x - origin.x, xmax + 64),
                        std::min(p.dimensions.y - origin.y, ymax + 64)};
    local.source = {p.source.x - origin.x, p.source.y - origin.y};
    for (auto &sink : local.sinks) {
        sink.x -= origin.x;
        sink.y -= origin.y;
    }
    return std::make_pair(std::move(local), origin);
}

Tree solve(Problem const &p) {
    auto domain = local_domain(p);
    if (!domain)
        return solve_core(p);
    std::optional<Tree> local;
    Integer local_score = 0;
    try {
        local = solve_core(domain->first);
        for (auto &node : *local) {
            node.position.x = add(node.position.x, domain->second.x);
            node.position.y = add(node.position.y, domain->second.y);
        }
        local_score = validate_tree(p, *local).score;
        auto const lower_bound = score_lower_bound(p);
        if (lower_bound && local_score == *lower_bound)
            return std::move(*local);
    } catch (std::runtime_error const &) {
        local.reset();
    }
    try {
        auto original = solve_core(p);
        if (local && local_score < validate_tree(p, original).score)
            return std::move(*local);
        return original;
    } catch (std::runtime_error const &) {
        if (local)
            return std::move(*local);
        throw;
    }
}

} // namespace cbi
