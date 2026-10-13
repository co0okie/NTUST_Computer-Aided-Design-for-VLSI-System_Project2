#pragma once
#include "core/tree.hpp"

namespace cbi {
Tree solve_reference(Problem const &p);
std::optional<Integer> score_lower_bound(Problem const &p);
Tree partial_hub_tree(Problem const &p, Tree best, std::size_t *shared_trials = nullptr,
                      bool prioritize_deficits = false);
Tree joint_bypass_tree(Problem const &p, Tree best, std::size_t *shared_work = nullptr);
std::vector<int> multiway_policies(Problem const &p);
std::optional<Tree> layered_candidate(Problem const &p);
std::optional<Tree> compressed_chain_candidate(Problem const &p);
Tree solve_core(Problem const &p);
std::optional<std::pair<Problem, Position>> local_domain(Problem const &p);
Tree solve(Problem const &p);
} // namespace cbi
