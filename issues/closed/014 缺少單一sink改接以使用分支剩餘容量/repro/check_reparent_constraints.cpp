#include "../../../../src/tree.hpp"
#include <iostream>

void unchanged(cbi::Problem const& p, cbi::Tree const& tree) {
    auto before = cbi::serialize_tree(p, tree);
    auto result = cbi::reparent_leaves(p, tree);
    if (cbi::serialize_tree(p, result) != before)
        throw std::runtime_error("invalid or empty-parent reparent accepted");
}

int main() {
    // Move a depth-one leaf below an existing unary buffer, upgrading its type.
    cbi::Problem p{{2, 22}, {40, 40}, {{"T0", 1, 30, 1}, {"T1", 2, 30, 2}},
                   1, {0, 0}, {{1, 1}, {20, 10}}};
    auto tree = cbi::initial_tree(p, false);
    tree.push_back({{10, 10}, 0, {2}}); tree[0].children = {1, 3};
    auto result = cbi::reparent_leaves(p, tree);
    if (cbi::validate_tree(p, result).score != 10 || result[3].type != 1)
        throw std::runtime_error("depth-changing reparent/type upgrade failed");
    auto full = p; full.buffer_library = {{"T0", 1, 30, 1}};
    unchanged(full, tree);  // Destination has no fanout capacity.
    auto short_wire = p; short_wire.buffer_library = {{"T0", 2, 20, 1}};
    unchanged(short_wire, tree);  // Each new edge fits 20, outgoing sum is 28.
    std::size_t budget = 0;
    if (cbi::serialize_tree(p, cbi::reparent_leaves(p, tree, &budget)) != cbi::serialize_tree(p, tree))
        throw std::runtime_error("exhausted budget changed tree");
    // Reparenting the late S1 to SRC would remove skew, but SRC sum is 50>40.
    cbi::Problem src{{2, 40}, {40, 40}, {{"T0", 2, 40, 1}},
                     1, {0, 0}, {{0, 30}, {20, 10}}};
    auto root = cbi::initial_tree(src, false);
    root.push_back({{10, 10}, 0, {1, 2}}); root[0].children = {3};
    unchanged(src, root);
    src.source_limits = {1, 100}; unchanged(src, root);  // SRC fanout is full.
    // Both old parents have one leaf; improvement cannot leave either empty.
    cbi::Problem empty{{2, 100}, {40, 40}, {{"T0", 2, 100, 1}},
                       1, {0, 0}, {{1, 1}, {20, 10}}};
    auto unary = cbi::initial_tree(empty, false);
    unary.push_back({{10, 10}, 0, {1}}); unary.push_back({{10, 20}, 0, {2}});
    unary[0].children = {3, 4}; unchanged(empty, unary);
    std::cout << cbi::serialize_tree(p, result);
}
