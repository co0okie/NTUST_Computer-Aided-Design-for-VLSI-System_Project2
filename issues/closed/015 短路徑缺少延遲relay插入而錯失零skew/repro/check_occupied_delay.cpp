#include "../../../../src/tree.hpp"
#include <iostream>

int main() {
    cbi::Problem p{{3, 100}, {10, 10}, {{"T0", 1, 100, 1}}, 100,
                   {5, 5}, {{7, 5}, {9, 5}, {6, 6}}};
    auto tree = cbi::initial_tree(p, false);
    tree.push_back({{6, 4}, 0, {3}}); tree[0].children = {1, 2, 4};
    if (cbi::validate_tree(p, tree).score != 201) return 1;
    std::size_t budget = 0, insertions = 0;
    if (cbi::serialize_tree(p, cbi::delay_tree(p, tree, &budget)) != cbi::serialize_tree(p, tree)) return 2;
    if (cbi::serialize_tree(p, cbi::delay_tree(p, tree, nullptr, &insertions)) != cbi::serialize_tree(p, tree)) return 3;
    insertions = 1;
    auto result = cbi::delay_tree(p, tree, nullptr, &insertions);
    if (cbi::validate_tree(p, result).score != 2 || insertions != 0) return 4;
    std::cout << cbi::serialize_tree(p, result);
}
