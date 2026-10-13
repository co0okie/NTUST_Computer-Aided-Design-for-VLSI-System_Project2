#pragma once
#include "core/tree.hpp"

namespace cbi {
std::vector<int> buffer_policies(Problem const &p, bool branching = false);
bool tree_less(Tree const &a, Tree const &b);
std::optional<Tree> small_forest(Problem const &p, std::size_t &remaining_trials,
                                 bool multiway = false, bool library_arity = false);
std::optional<Tree> dense_forest_search(Problem const &p, std::size_t &work,
                                        bool hierarchical = false);
std::optional<Tree> dense_forest(Problem const &p, std::size_t *shared_work = nullptr);
} // namespace cbi
