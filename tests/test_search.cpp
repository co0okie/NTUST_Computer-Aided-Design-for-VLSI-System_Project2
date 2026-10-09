#include "tree.hpp"

#include <iostream>

namespace {
void require(bool condition, char const* message) {
    if (!condition) throw std::runtime_error(message);
}

cbi::Problem grid(cbi::Integer side) {
    cbi::Problem p{{1, 1000}, {side, side}, {{"T0", 4, 1000, 1}}, 100, {side / 2, 0}, {}};
    for (cbi::Integer x = 1; x < side; x += 2)
        for (cbi::Integer y = 1; y < side; y += 2) p.sinks.push_back({x, y});
    return p;
}

void multiway() {
    for (auto side : {8, 16, 32}) {
        auto const p = grid(side);
        std::size_t work = 1048576;
        auto const tree = cbi::Builder(p, true, -1, 0).build_multiway(1, work);
        auto const metrics = cbi::validate_tree(p, tree);
        auto const buffers = (p.sinks.size() - 1) / 3;
        require(tree.size() == p.sinks.size() + 1 + buffers, "direct four-output buffer count");
        require(metrics.maximum_arrival == metrics.minimum_arrival, "grid zero skew");
        require(metrics.score == static_cast<cbi::Integer>(buffers), "grid optimal cost");
        require(cbi::score_lower_bound(p) == metrics.score, "attained lower bound");
        require(work < 1048576, "construction work is charged");
    }
    auto const p = grid(8);
    for (std::size_t work : {std::size_t{0}, std::size_t{1}}) {
        bool failed = false;
        try { (void)cbi::Builder(p, true, -1, 0).build_multiway(1, work); }
        catch (std::runtime_error const&) { failed = true; }
        require(failed && work == 0, "exhausted construction budget");
    }
    auto branching = grid(16);
    branching.source_limits = {4, 1000};
    std::size_t work = 1048576;
    auto const forest = cbi::Builder(branching, true, -1, 0).build_multiway(4, work);
    (void)cbi::validate_tree(branching, forest);
    require(forest[0].children.size() == 4, "independent source groups");
    branching.buffer_library[0].fanout = std::numeric_limits<cbi::Integer>::max();
    work = 1048576;
    auto const wide = cbi::Builder(branching, true, -1, 0).build_multiway(1, work);
    (void)cbi::validate_tree(branching, wide);
    for (auto const& node : wide) require(node.children.size() <= 32, "huge fanout remains bounded");
}

void lower_bounds() {
    auto p = grid(8);
    p.sinks.clear();
    require(cbi::score_lower_bound(p) == 0, "empty lower bound");
    p.sinks = {{1, 1}, {1, 2}, {2, 1}};
    p.source_limits = {2, 2};
    p.buffer_library = {{"T0", 4, 100, 2}};
    require(cbi::score_lower_bound(p) == 102, "source capacity and parity lower bound");
    p.source_limits = {100, 2};
    require(cbi::score_lower_bound(p) == 102, "wire limit bounds effective source fanout");
    p.source_limits = {1, 100};
    p.buffer_library = {{"T0", 100, 2, 2}};
    require(cbi::score_lower_bound(p) == 104, "wire limit bounds effective buffer fanout");
    p.buffer_library[0].fanout = 1;
    require(!cbi::score_lower_bound(p), "unary library cannot grow branches");
    p.buffer_library = {{"T0", 2, 100, std::numeric_limits<cbi::Integer>::max()}};
    require(!cbi::score_lower_bound(p), "lower bound multiplication overflow");
    p.source_limits = {2, 100};
    require(!cbi::score_lower_bound(p), "lower bound addition overflow");
}

void scheduler() {
    cbi::Problem const p{{3, 78}, {40, 40}, {{"T0", 4, 84, 1}}, 9, {12, 28},
                        {{34,20},{24,13},{38,19},{20,1},{9,35},{37,29},{1,12},{15,26},{34,18},{26,35}}};
    auto initial = cbi::initial_tree(p, false);
    initial.push_back({{9,5}, 0, {7,4}});
    initial.push_back({{23,31}, 0, {2,13,5}});
    initial.push_back({{28,29}, 0, {10,6}});
    initial.push_back({{29,27}, 0, {9,3,1,8}});
    initial.push_back({{15,20}, 0, {11,12,14}});
    initial[0].children = {15};
    auto const start = cbi::validate_tree(p, initial).score;
    require(start == 50, "scheduler fixture score");
    for (bool adaptive : {false, true}) {
        for (std::size_t allowance : {std::size_t{0}, std::size_t{1}, std::size_t{100}, std::size_t{100000}}) {
            auto trials = allowance;
            std::size_t insertions = 100;
            auto const tree = cbi::scheduled_refinement(p, initial, adaptive, &trials, &insertions);
            require(cbi::validate_tree(p, tree).score <= start, "refinement cannot raise actual score");
            require(trials <= allowance && allowance - trials <= (adaptive ? 8192 : 36864), "shared trial accounting");
            require(insertions <= 100 && 100 - insertions <= 16, "shared insertion accounting");
            if (!allowance) require(!cbi::tree_less(tree, initial) && !cbi::tree_less(initial, tree), "zero budget preserves tree");
            if (allowance == 100000) require(cbi::validate_tree(p, tree).score <= 34, "retry unlocked improvements");
        }
    }
    std::size_t trials = 8192, insertions = 0;
    auto const no_insertions = cbi::adaptive_refine(p, initial, &trials, &insertions);
    require(no_insertions.size() <= initial.size() && insertions == 0, "zero insertion budget");
    auto const large = grid(32);
    std::size_t work = 1048576;
    auto const tree = cbi::Builder(large, true, -1, 0).build_multiway(1, work);
    trials = 8192; insertions = 16;
    auto const unchanged = cbi::adaptive_refine(large, tree, &trials, &insertions);
    require(!cbi::tree_less(tree, unchanged) && !cbi::tree_less(unchanged, tree), "large tree cutoff");
    require(trials == 8192 && insertions == 16, "skipped search consumes no budget");
}

void retry_unlocked_move() {
    // A later move unlocks an earlier operator. One dispatch of each operator
    // stops at 16, while the adaptive queue revisits and reaches 12.
    cbi::Problem const p{{3, 5}, {20, 20},
                        {{"T0", 1, 5, 2}, {"T1", 5, 40, 10}, {"T2", 6, 10, 4}},
                        1, {18, 8}, {{8, 2}, {2, 8}}};
    auto const initial = cbi::Builder(p, true).build();
    auto sweep = initial;
    std::size_t trials = 512, insertions = 16;
    sweep = cbi::improve_tree(p, std::move(sweep), &trials); trials = 512;
    sweep = cbi::relocate_tree(p, std::move(sweep), &trials); trials = 512;
    sweep = cbi::swap_leaves(p, std::move(sweep), &trials); trials = 512;
    sweep = cbi::reparent_leaves(p, std::move(sweep), &trials); trials = 512;
    sweep = cbi::swap_subtrees(p, std::move(sweep), &trials); trials = 512;
    sweep = cbi::delay_tree(p, std::move(sweep), &trials, &insertions);
    require(cbi::validate_tree(p, sweep).score == 16, "single sweep reference score");
    trials = 8192; insertions = 16;
    auto const retried = cbi::adaptive_refine(p, initial, &trials, &insertions);
    require(cbi::validate_tree(p, retried).score <= 12, "adaptive queue retries unlocked move");
}
} // namespace

int main() {
    try { multiway(); lower_bounds(); scheduler(); retry_unlocked_move(); }
    catch (std::exception const& error) { std::cerr << error.what() << '\n'; return 1; }
}
