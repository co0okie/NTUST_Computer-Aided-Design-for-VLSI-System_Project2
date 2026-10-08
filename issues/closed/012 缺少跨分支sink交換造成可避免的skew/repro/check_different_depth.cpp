#include "../../../../src/tree.hpp"
#include <iostream>

int main() {
    cbi::Problem p{{2, 60}, {40, 40}, {{"T0", 1, 20, 1}}, 1,
                   {0, 0}, {{1, 1}, {20, 10}}};
    auto tree = cbi::initial_tree(p, false);
    tree.push_back({{10, 10}, 0, {2}});
    tree[0].children = {1, 3};
    if (cbi::validate_tree(p, tree).score != 29) return 1;
    tree = cbi::swap_leaves(p, std::move(tree));
    if (cbi::validate_tree(p, tree).score != 9) return 2;
    std::cout << cbi::serialize_tree(p, tree);
}
