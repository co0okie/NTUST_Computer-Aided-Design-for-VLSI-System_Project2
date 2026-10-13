#include "construction/builder.hpp"
#include "io/output.hpp"
#include "search/annealing.hpp"
#include "search/local.hpp"
#include "solver/strategy.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>

namespace {
void require(bool condition, char const *message) {
    if (!condition)
        throw std::runtime_error(message);
}
cbi::TimePoint deadline() {
    return std::chrono::steady_clock::now() + std::chrono::seconds(10);
}
cbi::Problem fixture() {
    return {{3, 200}, {20, 20}, {{"T0", 3, 100, 2}, {"T1", 5, 200, 8}},
            10,       {2, 2},   {{3, 9}, {12, 7}, {14, 14}, {7, 15}}};
}
cbi::Tree fixture_tree(cbi::Problem const &p) {
    auto tree = cbi::initial_tree(p, false);
    tree.emplace_back(cbi::Position{4, 6}, cbi::BufferTypeId{0}, std::vector<std::size_t>{1, 2});
    tree.emplace_back(cbi::Position{9, 10}, cbi::BufferTypeId{0}, std::vector<std::size_t>{3, 4});
    tree[0].children = {5, 6};
    return tree;
}
void owned_snapshots() {
    auto p = fixture();
    auto tree = fixture_tree(p);
    cbi::RelocationEvaluation snapshot(p, tree);
    auto const expected = snapshot.evaluate(5, tree[5].position);
    tree[5].position = {1, 1};
    p.buffer_library[0].cost = 999;
    auto const actual = snapshot.evaluate(5, {4, 6});
    require(actual.score == expected.score, "relocation snapshot owns its immutable inputs");
    // A stored builder remains valid after a temporary problem has been destroyed,
    // and moving the wrapper preserves the stable context.
    cbi::Builder temporary(fixture(), true);
    auto moved = std::move(temporary);
    auto const built = moved.build();
    require(!cbi::is_error(cbi::evaluate_tree(fixture(), built)),
            "builder owns temporary problem across moves");
}
void exact_check() {
    auto p = fixture();
    auto tree = fixture_tree(p);
    require(!cbi::is_error(cbi::evaluate_tree(p, tree)), "legal fixture");
    tree[5].position = p.source;
    require(cbi::is_error(cbi::evaluate_tree(p, tree)), "collision rejected");
    tree = fixture_tree(p);
    tree[5].children.push_back(6);
    tree[6].children.push_back(5);
    require(cbi::is_error(cbi::check_topology(p, tree)), "cycle and repeated parent rejected");
    require(!cbi::checked_add(std::numeric_limits<cbi::Integer>::max(), 1), "checked overflow");
    require(!cbi::checked_distance({-1, 0}, {1, 0}), "negative distance input rejected");
    p = fixture();
    tree = fixture_tree(p);
    p.source_limits.length = 1;
    require(cbi::is_error(cbi::evaluate_tree(p, tree)), "summed source length rejected");
}
void lp_exhaustive() {
    for (auto pins :
         {std::vector<cbi::Position>{{1, 5}, {5, 1}}, {{2, 5}, {5, 2}}, {{2, 4}, {5, 1}}}) {
        cbi::Problem const p{{1, 20}, {6, 6}, {{"T0", 2, 12, 1}}, 10, {0, 0}, pins};
        auto tree = cbi::initial_tree(p, false);
        tree.emplace_back(cbi::Position{1, 1}, cbi::BufferTypeId{0},
                          std::vector<std::size_t>{1, 2});
        tree[0].children = {3};
        for (unsigned q = 0; q < 4; ++q) {
            auto state = cbi::make_search_state(tree);
            state.directions[1].quadrant = static_cast<cbi::Quadrant>(q);
            cbi::Integer best = std::numeric_limits<cbi::Integer>::max();
            for (cbi::Integer x = 1; x < 6; ++x)
                for (cbi::Integer y = 1; y < 6; ++y) {
                    auto integer = state;
                    integer.tree[3].position = {x, y};
                    auto const metrics = cbi::evaluate_tree(p, integer.tree);
                    if (!cbi::is_error(metrics) && cbi::directions_hold(integer)) {
                        auto const &m = std::get<cbi::Metrics>(metrics);
                        best = std::min(best, m.maximum_arrival - m.minimum_arrival);
                    }
                }
            auto const placement = cbi::optimize_placement(p, state, {}, deadline());
            if (cbi::is_error(placement))
                require(best == std::numeric_limits<cbi::Integer>::max(),
                        "LP did not miss a feasible integer placement");
            else {
                require(std::get<cbi::Placement>(placement).skew >= -1e-6,
                        "nonnegative relaxation skew");
                if (best != std::numeric_limits<cbi::Integer>::max())
                    require(std::get<cbi::Placement>(placement).skew <=
                                static_cast<double>(best) + 1e-6,
                            "LP relaxation bounds integer skew");
                auto const repaired = cbi::repair_placement(
                    p, state, std::get<cbi::Placement>(placement), {}, deadline());
                if (!cbi::is_error(repaired))
                    require(!cbi::is_error(
                                cbi::evaluate_tree(p, std::get<cbi::SearchState>(repaired).tree)),
                            "repair returns legal tree");
            }
        }
    }
    auto p = fixture();
    auto state = cbi::make_search_state(fixture_tree(p));
    require(cbi::is_error(cbi::optimize_placement(p, state, {}, std::chrono::steady_clock::now())),
            "expired LP budget");
    p.dimensions.x = std::numeric_limits<cbi::Integer>::max();
    require(cbi::is_error(cbi::optimize_placement(p, state, {}, deadline())),
            "unsafe numerical domain skipped");
    state.capacity.clear();
    require(cbi::is_error(cbi::optimize_placement(p, state, {}, deadline())),
            "malformed capacity vector rejected");
}
void repair_and_types() {
    auto p = fixture();
    auto state = cbi::make_search_state(fixture_tree(p));
    cbi::Placement placement{{}, 0};
    for (auto const &node : state.tree)
        placement.positions.push_back(
            {static_cast<double>(node.position.x), static_cast<double>(node.position.y)});
    placement.positions[5] = placement.positions[0];
    auto repaired = cbi::repair_placement(p, state, placement, {}, deadline());
    require(!cbi::is_error(repaired),
            "collision repaired using original coordinate or local search");
    require(cbi::directions_hold(std::get<cbi::SearchState>(repaired)),
            "repair preserves directions");
    cbi::PlacementOptions options;
    options.repair_trials = 0;
    require(cbi::is_error(cbi::repair_placement(p, state, placement, options, deadline())),
            "repair budget enforced");
    auto malformed = state;
    malformed.capacity[5] = cbi::BufferTypeId{999};
    require(cbi::is_error(cbi::repair_placement(p, malformed, placement, {}, deadline())),
            "repair rejects invalid capacity without indexing it");
    malformed = state;
    malformed.directions.clear();
    require(cbi::is_error(cbi::repair_placement(p, malformed, placement, {}, deadline())),
            "repair rejects missing directions");
    placement.positions[5].x = std::numeric_limits<double>::infinity();
    require(cbi::is_error(cbi::repair_placement(p, state, placement, {}, deadline())),
            "nonfinite rounding rejected");
    p.buffer_library.push_back({"dominated", 2, 50, 9});
    p.buffer_library.push_back(p.buffer_library[0]);
    auto types = cbi::undominated_types(p);
    require(types.size() == 2, "dominated and duplicate capacities pruned");
    state = cbi::make_search_state(fixture_tree(p));
    state.capacity[5] = cbi::BufferTypeId{1};
    placement.positions.clear();
    for (auto const &node : state.tree)
        placement.positions.push_back(
            {static_cast<double>(node.position.x), static_cast<double>(node.position.y)});
    repaired = cbi::repair_placement(p, state, placement, {}, deadline());
    require(!cbi::is_error(repaired), "capacity normalization fixture");
    auto const &result = std::get<cbi::SearchState>(repaired);
    require(result.capacity[5]->value == 1 && result.tree[5].type_index() == 0,
            "search capacity survives cheapest output normalization");
}
void perturbations() {
    auto const p = fixture();
    auto const initial = cbi::make_search_state(fixture_tree(p));
    auto const original = cbi::serialize_tree(p, initial.tree);
    cbi::AnnealingOptions options;
    std::mt19937_64 rng(7);
    for (std::size_t move = 0; move < cbi::move_count; ++move) {
        unsigned valid = 0;
        for (unsigned trial = 0; trial < 40; ++trial) {
            auto candidate =
                cbi::perturb_state(p, initial, static_cast<cbi::AnnealingMove>(move), options, rng);
            if (!cbi::is_error(candidate)) {
                ++valid;
                auto const &state = std::get<cbi::SearchState>(candidate);
                require(!cbi::is_error(cbi::check_topology(p, state.tree)),
                        "move preserves structural invariants");
                require(state.tree.size() == state.capacity.size() &&
                            state.tree.size() == state.directions.size(),
                        "parallel vectors remapped");
            }
            require(original == cbi::serialize_tree(p, initial.tree),
                    "perturbation rollback preserves input");
        }
        require(valid > 0, "every move has a supported fixture");
    }
}
void annealing() {
    auto const p = fixture();
    auto const initial = fixture_tree(p);
    cbi::AnnealingOptions options;
    options.iterations = 40;
    options.seed = 41;
    auto a = cbi::anneal(p, initial, options, deadline()),
         b = cbi::anneal(p, initial, options, deadline());
    require(!cbi::is_error(a) && !cbi::is_error(b), "annealing succeeds");
    auto const &ra = std::get<cbi::AnnealingResult>(a);
    auto const &rb = std::get<cbi::AnnealingResult>(b);
    require(ra.metrics.score <= cbi::validate_tree(p, initial).score, "incumbent cannot regress");
    require(cbi::serialize_tree(p, ra.best.tree) == cbi::serialize_tree(p, rb.best.tree),
            "fixed seed and iterations reproduce result");
    require(!cbi::is_error(cbi::evaluate_tree(p, ra.best.tree)), "best tree is legal");
    auto expired = cbi::anneal(p, initial, options, std::chrono::steady_clock::now());
    require(!cbi::is_error(expired) && std::get<cbi::AnnealingResult>(expired).metrics.score ==
                                           cbi::validate_tree(p, initial).score,
            "deadline preserves legal incumbent");
    std::mt19937_64 invalid_rng(1);
    require(cbi::is_error(cbi::perturb_state(p, cbi::make_search_state(initial),
                                             static_cast<cbi::AnnealingMove>(99), options,
                                             invalid_rng)),
            "invalid move enum rejected");
    options.locality_k = 0;
    require(cbi::is_error(cbi::anneal(p, initial, options, deadline())),
            "invalid sampling parameters rejected");
}
} // namespace
int main() {
    try {
        owned_snapshots();
        exact_check();
        lp_exhaustive();
        repair_and_types();
        perturbations();
        annealing();
    } catch (std::exception const &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
