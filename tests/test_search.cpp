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

void compare_relocations(cbi::Problem const& p, cbi::Tree const& tree,
                         std::vector<cbi::Position> const& points) {
    cbi::RelocationEvaluation const evaluation(p, tree);
    for (std::size_t moved = p.sinks.size() + 1; moved < tree.size(); ++moved) {
        for (auto point : points) {
            std::optional<cbi::Metrics> expected, actual;
            try {
                auto candidate = tree; candidate[moved].position = point;
                cbi::select_buffer_types(p, candidate);
                expected = cbi::validate_tree(p, candidate);
            } catch (std::runtime_error const&) {}
            try { actual = evaluation.evaluate(moved, point); }
            catch (std::runtime_error const&) {}
            require(expected.has_value() == actual.has_value(), "relocation legality matches full validation");
            if (expected) {
                require(expected->score == actual->score &&
                        expected->maximum_arrival == actual->maximum_arrival &&
                        expected->minimum_arrival == actual->minimum_arrival,
                        "cached relocation metrics match full validation");
            }
        }
    }
}

void incremental_relocations() {
    auto p = grid(8);
    std::size_t work = 1048576;
    auto tree = cbi::Builder(p, true, -1, 0).build_multiway(1, work);
    std::vector<cbi::Position> points;
    for (cbi::Integer x = 0; x <= 8; ++x)
        for (cbi::Integer y = 0; y <= 8; ++y) points.push_back({x, y});
    compare_relocations(p, tree, points);
    // All unchanged buffers must also receive their cheapest compatible type.
    p.buffer_library.push_back({"T1", 4, 1000, 7});
    for (std::size_t i = p.sinks.size() + 1; i < tree.size(); ++i) {
        tree[i].type = 1;
        // Construction's virtual offsets must never replace actual arrivals.
        tree[i].low = 12345; tree[i].high = 67890;
    }
    compare_relocations(p, tree, points);
    p.source_limits.length = cbi::distance(p.source, tree[tree[0].children[0]].position);
    p.buffer_library[0].length = 16; p.buffer_library[1].length = 16;
    compare_relocations(p, tree, points);

    // Several source groups leave unchanged sink arrivals outside the moved
    // subtree; these participate in both extrema, even at different depths.
    p = grid(16); p.source_limits.fanout = 4; work = 1048576;
    tree = cbi::Builder(p, true, -1, 0).build_multiway(4, work);
    compare_relocations(p, tree, points);

    auto const maximum = std::numeric_limits<cbi::Integer>::max();
    p = {{1, maximum}, {maximum, maximum}, {{"T0", 2, maximum, 1}},
         1, {0, 0}, {{10, 10}, {10, 12}}};
    tree = cbi::initial_tree(p, false);
    tree.push_back({{9, 11}, 0, {1, 2}}); tree[0].children = {3};
    compare_relocations(p, tree, {{9, 11}, {maximum - 1, 1}, {1, maximum - 1}, {maximum - 1, maximum - 1}});
    // Overflow in weighted skew and buffer-cost reselection must be rejected.
    p.skew_weight = maximum;
    compare_relocations(p, tree, {{9, 11}, {9, 10}, {11, 12}});
    p.skew_weight = 1;
    p.buffer_library = {{"T0", 2, 4, 1}, {"T1", 2, maximum, maximum}};
    compare_relocations(p, tree, {{9, 11}, {9, 10}, {8, 8}});
}

void dense_forest_budgets() {
    cbi::Problem p{{3, 10}, {4, 4}, {{"T0", 9, 16, 1}}, 1, {4, 3},
                  {{2,4},{3,4},{0,0},{4,0},{4,1},{0,3},{2,2},{3,3},
                   {2,0},{1,0},{4,4},{1,4},{1,2},{3,0},{4,2},{2,3}}};
    for (std::size_t cap : {std::size_t{0}, std::size_t{1}, std::size_t{100}, std::size_t{1000000}}) {
        auto work = cap;
        auto result = cbi::dense_forest(p, &work);
        require(work <= cap && cap - work <= 262144, "dense forest shared work accounting");
        if (result) (void)cbi::validate_tree(p, *result);
        if (cap <= 1) require(!result && work == 0, "dense forest exhausted budget");
        if (cap == 1000000) require(result.has_value(), "dense forest constructs sixteen sinks");
    }
    p.source_limits.fanout = 1;
    std::size_t work = 100;
    require(!cbi::dense_forest(p, &work), "impossible source capacity remains bounded");
    auto const sparse = grid(8); work = 1000;
    require(!cbi::dense_forest(sparse, &work) && work == 1000, "many free positions skip dense search");
    auto large = sparse; large.dimensions = {std::numeric_limits<cbi::Integer>::max(), 100};
    require(!cbi::dense_forest(large, &work) && work == 1000, "large area does not overflow or scan");
}

void joint_hub_budgets() {
    cbi::Problem const p{{3, 23}, {15, 15}, {{"T0", 3, 16, 6}}, 3, {2, 12}, {{2, 2}, {13, 11}, {6, 7}}};
    auto initial = cbi::initial_tree(p, false);
    initial.push_back({{7, 11}, 0, {3, 2}}); initial[0].children = {1, 4};
    require(cbi::validate_tree(p, initial).score == 12, "joint move plateau fixture");
    for (std::size_t cap : {std::size_t{0}, std::size_t{1}, std::size_t{100}, std::size_t{100000}}) {
        auto trials = cap;
        auto const result = cbi::partial_hub_tree(p, initial, &trials);
        require(trials <= cap && cap - trials <= 8192, "joint hub shared trial accounting");
        require(cbi::validate_tree(p, result).score <= 12, "joint hub cannot regress score");
        if (!cap) require(!cbi::tree_less(result, initial) && !cbi::tree_less(initial, result), "joint hub zero budget");
        if (cap == 100000) require(cbi::validate_tree(p, result).score == 9, "joint hub crosses plateau to lower bound");
    }
    auto const large = grid(16); std::size_t work = 1048576;
    auto const tree = cbi::Builder(large, true, -1, 0).build_multiway(1, work);
    std::size_t trials = 8192;
    auto const unchanged = cbi::partial_hub_tree(large, tree, &trials);
    require(!cbi::tree_less(unchanged, tree) && !cbi::tree_less(tree, unchanged) && trials == 8192,
            "joint hub cutoff preserves tree and budget");
}

cbi::Problem rings(cbi::Integer radius) {
    auto const dimension = radius < 32 ? 160 : radius * 10;
    auto const center = dimension / 2;
    auto const span = radius < 32 ? 20 : radius * 5 / 2;
    cbi::Problem p{{1, 1}, {dimension, dimension},
                   {{"T0", 4 * radius, std::max(4 * radius * radius, 4 * span), 1}},
                   100, {center, center - 1}, {}};
    for (cbi::Position hub : {cbi::Position{center - span, center}, {center + span, center},
                             {center, center - span}, {center, center + span}}) {
        for (cbi::Integer dx = -radius; dx <= radius; ++dx) {
            auto const dy = radius - (dx < 0 ? -dx : dx);
            p.sinks.push_back({hub.x + dx, hub.y + dy});
            if (dy) p.sinks.push_back({hub.x + dx, hub.y - dy});
        }
    }
    return p;
}

void layered_rings() {
    for (cbi::Integer radius : {2, 8, 16, 32, 64}) {
        auto p = rings(radius);
        for (bool reverse : {false, true}) {
            if (reverse) std::reverse(p.sinks.begin(), p.sinks.end());
            std::size_t work = 262144;
            auto const tree = cbi::Builder(p, true, -1, 0).build_layered(4, work);
            auto const metrics = cbi::validate_tree(p, tree);
            require(metrics.score == 5 && metrics.minimum_arrival == metrics.maximum_arrival, "layered rings attain optimal five buffers");
            require(tree.size() == p.sinks.size() + 6, "layered ring buffer count");
            require(work < 262144 && work > 0, "layered construction charges distance and edge work");
        }
    }
    auto const p = rings(8);
    for (std::size_t work : {std::size_t{0}, std::size_t{1}, std::size_t{100}}) {
        bool failed = false;
        try { (void)cbi::Builder(p, true, -1, 0).build_layered(4, work); }
        catch (std::runtime_error const&) { failed = true; }
        require(failed && work == 0, "layered budget exhaustion has no underflow");
    }
}

void joint_bypass_budgets() {
    cbi::Problem p{{4, 6}, {9, 9}, {{"T0", 2, 14, 3}}, 34, {5, 3}, {{6, 1}, {4, 9}, {0, 3}, {6, 3}}};
    auto initial = cbi::initial_tree(p, false);
    initial.push_back({{4, 3}, 0, {3, 1}}); initial.push_back({{4, 5}, 0, {4, 2}});
    initial.push_back({{4, 4}, 0, {5, 6}}); initial[0].children = {7};
    require(cbi::validate_tree(p, initial).score == 9, "joint bypass plateau fixture");
    for (std::size_t cap : {std::size_t{0}, std::size_t{1}, std::size_t{100}, std::size_t{100000}}) {
        auto work = cap;
        auto const result = cbi::joint_bypass_tree(p, initial, &work);
        auto const metrics = cbi::validate_tree(p, result);
        require(work <= cap && cap - work <= 8192, "joint bypass shared work accounting");
        require(metrics.score <= 9, "joint bypass cannot regress score");
        if (!cap) require(!cbi::tree_less(result, initial) && !cbi::tree_less(initial, result), "joint bypass zero budget");
        if (cap == 100000) require(metrics.score == 6 && metrics.maximum_arrival == metrics.minimum_arrival, "joint bypass keeps zero skew while deleting buffer");
    }
    p.source_limits.fanout = 1;
    auto const blocked = cbi::joint_bypass_tree(p, initial);
    require(cbi::validate_tree(p, blocked).score == 9, "joint bypass obeys source fanout");
}

void local_domains() {
    auto const maximum = std::numeric_limits<cbi::Integer>::max();
    cbi::Problem p{{1, 1}, {maximum, maximum}, {{"T0", 13, 52, 1}}, 100, {10, 9},
                  {{6,10},{7,11},{7,9},{8,12},{8,8},{9,13},{9,7},
                   {10,14},{10,6},{11,13},{11,7},{12,12},{12,8}}};
    for (auto offset : {cbi::Integer{0}, maximum - 20}) {
        auto shifted = p;
        shifted.source.x += offset; shifted.source.y += offset;
        for (auto& sink : shifted.sinks) { sink.x += offset; sink.y += offset; }
        auto domain = cbi::local_domain(shifted);
        require(domain && domain->first.dimensions.x < 200 && domain->first.dimensions.y < 200,
                "compact pins have compact translated domain");
        auto const result = cbi::solve(shifted);
        require(cbi::validate_tree(shifted, result).score == 1, "translated huge-domain hub remains legal and optimal");
    }
    p = {{1, 1}, {maximum, maximum}, {{"T0", 2, 10, 1}}, 100, {maximum, 5}, {{maximum, 1}, {maximum, 9}}};
    auto const boundary = cbi::solve(p);
    require(cbi::validate_tree(p, boundary).score == 1, "translation preserves extreme original boundary");
    p = {{1, maximum}, {maximum, maximum}, {{"T0", 1, maximum, 1}}, 1, {0, 0}, {{maximum, maximum}}};
    require(!cbi::local_domain(p), "unrepresentable span is not clipped to lose pins");
    bool failed = false;
    try { (void)cbi::solve(p); } catch (std::runtime_error const&) { failed = true; }
    require(failed, "actual Manhattan arrival overflow remains rejected");
}
} // namespace

int main() {
    try {
        multiway(); lower_bounds(); scheduler(); retry_unlocked_move();
        incremental_relocations(); dense_forest_budgets(); joint_hub_budgets(); layered_rings(); joint_bypass_budgets(); local_domains();
    }
    catch (std::exception const& error) { std::cerr << error.what() << '\n'; return 1; }
}
