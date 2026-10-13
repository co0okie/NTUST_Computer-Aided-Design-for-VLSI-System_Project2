#include "search/annealing.hpp"
#include "solver/legacy.hpp"
#include <algorithm>
#include <cmath>
#include <numeric>

namespace cbi {
namespace {
std::size_t sample(std::size_t count, std::mt19937_64 &rng) {
    return std::uniform_int_distribution<std::size_t>(0, count - 1)(rng);
}
double unit(std::mt19937_64 &rng) {
    return std::generate_canonical<double, 53>(rng);
}
void set_direction(SearchState &state, std::size_t parent, std::size_t child) {
    auto const a = state.tree[parent].position, b = state.tree[child].position;
    state.directions[child] =
        Direction{b.x >= a.x ? (b.y >= a.y ? Quadrant::northeast : Quadrant::southeast)
                             : (b.y >= a.y ? Quadrant::northwest : Quadrant::southwest)};
}
void remove_child(Tree &tree, std::size_t parent, std::size_t child) {
    auto &children = tree[parent].children;
    children.erase(std::find(children.begin(), children.end(), child));
}
std::size_t nearby(Tree const &tree, std::size_t node, std::vector<std::size_t> candidates,
                   AnnealingOptions const &options, std::mt19937_64 &rng) {
    if (unit(rng) < options.locality_probability) {
        auto metric = [&](std::size_t u) {
            return checked_distance(tree[node].position, tree[u].position)
                .value_or(std::numeric_limits<Integer>::max());
        };
        std::sort(candidates.begin(), candidates.end(), [&](auto a, auto b) {
            auto const da = metric(a), db = metric(b);
            return da != db ? da < db : a < b;
        });
        candidates.resize(std::min(options.locality_k, candidates.size()));
    }
    return candidates[sample(candidates.size(), rng)];
}
void erase_buffer(SearchState &state, std::size_t victim) {
    // Pins occupy the fixed prefix. Compact every parallel state vector together.
    state.tree.erase(state.tree.begin() + static_cast<std::ptrdiff_t>(victim));
    state.capacity.erase(state.capacity.begin() + static_cast<std::ptrdiff_t>(victim));
    state.directions.erase(state.directions.begin() + static_cast<std::ptrdiff_t>(victim));
    for (auto &node : state.tree)
        for (auto &child : node.children)
            if (child > victim)
                --child;
}
} // namespace
Expected<SearchState> perturb_state(Problem const &p, SearchState state, AnnealingMove move,
                                    AnnealingOptions const &options, std::mt19937_64 &rng) {
    auto const valid_options = validate_annealing_options(options);
    if (is_error(valid_options))
        return std::get<Error>(valid_options);
    if (static_cast<unsigned>(move) >= move_count)
        return Error{"invalid annealing move", ErrorKind::input};
    auto const checked = check_search_state(p, state);
    if (is_error(checked))
        return std::get<Error>(checked);
    if (state.capacity.size() != state.tree.size() || state.directions.size() != state.tree.size())
        return Error{"search state dimensions disagree", ErrorKind::structure};
    auto const &topology = std::get<Topology>(checked);
    auto const buffers = state.tree.size() - p.sinks.size() - 1;
    auto buffer = [&]() { return p.sinks.size() + 1 + sample(buffers, rng); };
    switch (move) {
    case AnnealingMove::move_sink:
    case AnnealingMove::move_subtree: {
        if (move == AnnealingMove::move_subtree && !buffers)
            return Error{"no subtree to move", ErrorKind::structure};
        auto const node =
            move == AnnealingMove::move_sink ? 1 + sample(p.sinks.size(), rng) : buffer();
        auto const from = topology.parents[node].value;
        std::vector<bool> below(state.tree.size());
        std::vector<std::size_t> queue{node};
        below[node] = true;
        std::size_t sinks = 0;
        for (std::size_t k = 0; k < queue.size(); ++k) {
            auto const u = queue[k];
            if (state.tree[u].is_sink())
                ++sinks;
            for (auto v : state.tree[u].children) {
                below[v] = true;
                queue.push_back(v);
            }
        }
        if (move == AnnealingMove::move_subtree && sinks > options.subtree_sinks)
            return Error{"subtree exceeds move budget", ErrorKind::structure};
        if (from != 0 && state.tree[from].children.size() == 1)
            return Error{"move would leave unused buffer", ErrorKind::structure};
        std::vector<std::size_t> candidates;
        for (std::size_t u = 0; u < state.tree.size(); ++u)
            if (!state.tree[u].is_sink() && u != from && !below[u] &&
                (u != 0 || state.tree[u].children.size() <
                               static_cast<std::uint64_t>(p.source_limits.fanout)))
                candidates.push_back(u);
        if (candidates.empty())
            return Error{"no reparent candidate", ErrorKind::structure};
        auto const to = nearby(state.tree, node, std::move(candidates), options, rng);
        remove_child(state.tree, from, node);
        state.tree[to].children.push_back(node);
        set_direction(state, to, node);
        break;
    }
    case AnnealingMove::swap_sinks: {
        if (p.sinks.size() < 2)
            return Error{"too few sinks to swap", ErrorKind::structure};
        auto const a = 1 + sample(p.sinks.size(), rng), pa = topology.parents[a].value;
        std::vector<std::size_t> candidates;
        for (std::size_t b = 1; b <= p.sinks.size(); ++b)
            if (topology.parents[b].value != pa)
                candidates.push_back(b);
        if (candidates.empty())
            return Error{"no different parent to swap", ErrorKind::structure};
        auto const b = nearby(state.tree, a, std::move(candidates), options, rng),
                   pb = topology.parents[b].value;
        auto &ca = state.tree[pa].children;
        auto &cb = state.tree[pb].children;
        *std::find(ca.begin(), ca.end(), a) = b;
        *std::find(cb.begin(), cb.end(), b) = a;
        set_direction(state, pa, b);
        set_direction(state, pb, a);
        break;
    }
    case AnnealingMove::insert_buffer: {
        if (state.tree.size() >= options.placement.max_lp_nodes)
            return Error{"insertion exceeds LP budget", ErrorKind::structure};
        std::vector<std::size_t> parents;
        for (std::size_t u = 0; u < state.tree.size(); ++u)
            if (!state.tree[u].children.empty())
                parents.push_back(u);
        if (parents.empty())
            return Error{"no children to group", ErrorKind::structure};
        auto const parent = parents[sample(parents.size(), rng)];
        auto group = state.tree[parent].children;
        auto const anchor = group[sample(group.size(), rng)];
        std::sort(group.begin(), group.end(), [&](auto a, auto b) {
            auto const da = checked_distance(state.tree[anchor].position, state.tree[a].position)
                                .value_or(std::numeric_limits<Integer>::max());
            auto const db = checked_distance(state.tree[anchor].position, state.tree[b].position)
                                .value_or(std::numeric_limits<Integer>::max());
            return da != db ? da < db : a < b;
        });
        group.resize(1 + sample(std::min(std::size_t{4}, group.size()), rng));
        std::vector<Integer> xs, ys;
        for (auto child : group) {
            xs.push_back(state.tree[child].position.x);
            ys.push_back(state.tree[child].position.y);
        }
        std::sort(xs.begin(), xs.end());
        std::sort(ys.begin(), ys.end());
        auto const id = state.tree.size();
        state.tree.emplace_back(Position{xs[xs.size() / 2], ys[ys.size() / 2]}, BufferTypeId{0},
                                group);
        state.capacity.push_back(std::nullopt);
        state.directions.push_back({});
        for (auto child : group) {
            remove_child(state.tree, parent, child);
            set_direction(state, id, child);
        }
        state.tree[parent].children.push_back(id);
        set_direction(state, parent, id);
        break;
    }
    case AnnealingMove::delete_buffer: {
        if (!buffers)
            return Error{"no buffer to delete", ErrorKind::structure};
        auto const victim = buffer(), parent = topology.parents[victim].value;
        auto const children = state.tree[victim].children;
        remove_child(state.tree, parent, victim);
        for (auto child : children) {
            state.tree[parent].children.push_back(child);
            set_direction(state, parent, child);
        }
        erase_buffer(state, victim);
        break;
    }
    case AnnealingMove::change_direction: {
        auto const child = 1 + sample(state.tree.size() - 1, rng);
        auto const old = static_cast<unsigned>(state.directions[child].quadrant);
        state.directions[child].quadrant = static_cast<Quadrant>((old + 1 + sample(3, rng)) % 4);
        break;
    }
    case AnnealingMove::change_type: {
        if (!buffers)
            return Error{"no buffer type to change", ErrorKind::structure};
        auto const u = buffer();
        auto const types = undominated_types(p);
        std::vector<BufferTypeId> candidates;
        for (auto type : types)
            if ((!state.capacity[u] || type.value != state.capacity[u]->value) &&
                state.tree[u].children.size() <=
                    static_cast<std::uint64_t>(p.buffer_library[type.value].fanout))
                candidates.push_back(type);
        if (candidates.empty())
            return Error{"no alternative capacity", ErrorKind::capacity};
        state.capacity[u] = candidates[sample(candidates.size(), rng)];
        break;
    }
    }
    if (is_error(check_topology(p, state.tree)))
        return Error{"perturbation produced invalid structure", ErrorKind::structure};
    return state;
}
namespace {
Expected<SearchState> evaluate_candidate(Problem const &p, SearchState state,
                                         AnnealingOptions const &options, TimePoint deadline,
                                         SearchStatistics &statistics, bool preserve_type) {
    auto const estimate =
        preserve_type ? Expected<WriteSuccess>{WriteSuccess{}} : estimate_capacity(p, state, false);
    if (is_error(estimate))
        return std::get<Error>(estimate);
    Error last{"no legal candidate", ErrorKind::solver};
    for (unsigned attempt = 0; attempt < 3; ++attempt) {
        if (std::chrono::steady_clock::now() >= deadline)
            return Error{"candidate deadline reached", ErrorKind::timeout};
        if (attempt > 0) {
            auto const previous = state.capacity;
            auto const capacity = estimate_capacity(p, state, true);
            if (is_error(capacity))
                return std::get<Error>(capacity);
            // Identical capacities repeat the same failed placement/repair.
            // Continue to the structural attempt, which changes the topology.
            if (attempt == 1 &&
                std::equal(previous.begin(), previous.end(), state.capacity.begin(),
                           state.capacity.end(), [](auto a, auto b) {
                               return (!a && !b) || (a && b && a->value == b->value);
                           }))
                continue;
            if (attempt == 2) {
                // One bounded structural repair, after placement/capacity attempts.
                if (!options.structural_repair)
                    break;
                std::mt19937_64 repair_rng(options.seed + state.tree.size());
                auto inserted =
                    perturb_state(p, state, AnnealingMove::insert_buffer, options, repair_rng);
                if (is_error(inserted))
                    break;
                state = std::get<SearchState>(std::move(inserted));
                if (is_error(estimate_capacity(p, state, true)))
                    break;
            }
        }
        auto const start = std::chrono::steady_clock::now();
        ++statistics.lp_calls;
        auto placement = optimize_placement(p, state, options.placement, deadline);
        statistics.lp_seconds +=
            std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        if (is_error(placement)) {
            last = std::get<Error>(placement);
            if (last.kind == ErrorKind::numerical || last.kind == ErrorKind::timeout)
                break;
            continue;
        }
        ++statistics.lp_feasible;
        ++statistics.repair_calls;
        auto const repair_start = std::chrono::steady_clock::now();
        auto repaired =
            repair_placement(p, state, std::get<Placement>(placement), options.placement, deadline);
        statistics.repair_seconds +=
            std::chrono::duration<double>(std::chrono::steady_clock::now() - repair_start).count();
        if (!is_error(repaired)) {
            ++statistics.repair_feasible;
            return repaired;
        }
        last = std::get<Error>(repaired);
    }
    return last;
}
} // namespace
Expected<WriteSuccess> validate_annealing_options(AnnealingOptions const &options) {
    if (options.locality_k == 0 || !std::isfinite(options.locality_probability) ||
        options.locality_probability < 0 || options.locality_probability > 1 ||
        !std::isfinite(options.cooling_ratio) || options.cooling_ratio <= 0 ||
        options.cooling_ratio > 1 || !std::isfinite(options.initial_temperature) ||
        options.initial_temperature < 0 ||
        std::any_of(
            options.move_weights.begin(), options.move_weights.end(),
            [](auto w) { return !std::isfinite(w) || w < 0; }) ||
        (!std::isfinite(
             std::accumulate(options.move_weights.begin(), options.move_weights.end(), 0.0)) ||
         std::accumulate(options.move_weights.begin(), options.move_weights.end(), 0.0) <= 0) ||
        !std::isfinite(options.placement.lp_seconds) || options.placement.lp_seconds <= 0 ||
        options.placement.max_lp_nodes == 0)
        return Error{"invalid annealing parameters", ErrorKind::input};
    return WriteSuccess{};
}
Expected<AnnealingResult> anneal(Problem const &p, Tree initial, AnnealingOptions const &options,
                                 TimePoint deadline) {
    auto const initial_metrics = evaluate_tree(p, initial);
    if (is_error(initial_metrics))
        return std::get<Error>(initial_metrics);
    auto const valid_options = validate_annealing_options(options);
    if (is_error(valid_options))
        return std::get<Error>(valid_options);
    auto current = make_search_state(std::move(initial));
    auto current_metrics = std::get<Metrics>(initial_metrics);
    AnnealingResult result{current, current_metrics, {}, {{0, current_metrics}}};
    auto const start = std::chrono::steady_clock::now();
    auto const duration = std::max(0.001, std::chrono::duration<double>(deadline - start).count());
    std::mt19937_64 rng(options.seed);
    std::discrete_distribution<std::size_t> move_distribution(options.move_weights.begin(),
                                                              options.move_weights.end());
    std::vector<double> positive_deltas;
    auto temperature = options.initial_temperature > 0
                           ? options.initial_temperature
                           : std::max(1.0, static_cast<double>(current_metrics.score) * 0.02);
    auto const lower_bound = score_lower_bound(p);
    auto keep = [&](SearchState candidate, Metrics metrics) {
        current = std::move(candidate);
        current_metrics = metrics;
        if (metrics.score < result.metrics.score) {
            result.best = current;
            result.metrics = metrics;
            result.checkpoints.push_back(
                {std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count(),
                 metrics});
        }
    };
    // First compare LP refinement of the seed, without any discrete move.
    auto refined = evaluate_candidate(p, current, options, deadline, result.statistics, true);
    if (!is_error(refined)) {
        auto candidate = std::get<SearchState>(std::move(refined));
        auto const metrics = std::get<Metrics>(evaluate_tree(p, candidate.tree));
        if (metrics.score <= current_metrics.score)
            keep(std::move(candidate), metrics);
    }
    for (std::size_t iteration = 0; (!options.iterations || iteration < options.iterations) &&
                                    std::chrono::steady_clock::now() < deadline;
         ++iteration) {
        if (lower_bound && result.metrics.score == *lower_bound)
            break;
        auto const move_index = move_distribution(rng);
        auto const move = static_cast<AnnealingMove>(move_index);
        auto &stats = result.statistics.moves[move_index];
        ++stats.attempts;
        auto const move_start = std::chrono::steady_clock::now();
        auto candidate = perturb_state(p, current, move, options, rng);
        if (!is_error(candidate))
            candidate =
                evaluate_candidate(p, std::get<SearchState>(std::move(candidate)), options,
                                   deadline, result.statistics, move == AnnealingMove::change_type);
        if (!is_error(candidate)) {
            auto next = std::get<SearchState>(std::move(candidate));
            auto const evaluated = evaluate_tree(p, next.tree);
            if (!is_error(evaluated)) {
                ++stats.feasible;
                auto const metrics = std::get<Metrics>(evaluated);
                auto const delta = static_cast<long double>(metrics.score) -
                                   static_cast<long double>(current_metrics.score);
                if (delta > 0 && positive_deltas.size() < 24)
                    positive_deltas.push_back(static_cast<double>(delta));
                if (options.initial_temperature == 0 && positive_deltas.size() == 24) {
                    auto deltas = positive_deltas;
                    std::sort(deltas.begin(), deltas.end());
                    temperature = deltas[deltas.size() / 2] / -std::log(0.8);
                    positive_deltas.push_back(0);
                }
                auto const fraction =
                    options.iterations
                        ? static_cast<double>(iteration) / static_cast<double>(options.iterations)
                        : std::chrono::duration<double>(std::chrono::steady_clock::now() - start)
                                  .count() /
                              duration;
                auto const t = std::max(
                    1e-12, temperature * std::pow(options.cooling_ratio, std::min(1.0, fraction)));
                if (delta <= 0 || unit(rng) < std::exp(-static_cast<double>(delta) / t)) {
                    ++stats.accepted;
                    if (metrics.score < result.metrics.score)
                        ++stats.improvements;
                    keep(std::move(next), metrics);
                }
            }
        }
        stats.seconds +=
            std::chrono::duration<double>(std::chrono::steady_clock::now() - move_start).count();
    }
    return result;
}
} // namespace cbi
