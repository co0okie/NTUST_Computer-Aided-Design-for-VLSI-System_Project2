#pragma once
#include "core/tree.hpp"
#include <memory>

namespace cbi {
class RelocationEvaluationImpl;
class RelocationEvaluation final {
    std::unique_ptr<RelocationEvaluationImpl> impl_;

  public:
    RelocationEvaluation(Problem const &, Tree const &);
    ~RelocationEvaluation();
    RelocationEvaluation(RelocationEvaluation const &) = delete;
    RelocationEvaluation &operator=(RelocationEvaluation const &) = delete;
    std::size_t parent(std::size_t node) const;
    bool occupied_at(Position point) const;
    Metrics evaluate(std::size_t moved, Position point) const;
};

std::size_t local_trial_limit(Problem const &p, Tree const &tree, std::size_t *remaining_trials);
Tree contract_large_tree(Problem const &p, Tree const &original,
                         std::size_t *shared_work = nullptr);
Tree improve_tree(Problem const &p, Tree best, std::size_t *remaining_trials = nullptr);
Tree relocate_tree(Problem const &p, Tree best, std::size_t *remaining_trials = nullptr);
Tree swap_roots(Problem const &p, Tree best, std::size_t *remaining_trials, bool subtrees);
Tree swap_leaves(Problem const &p, Tree best, std::size_t *remaining_trials = nullptr);
Tree swap_subtrees(Problem const &p, Tree best, std::size_t *remaining_trials = nullptr);
Tree reparent_leaves(Problem const &p, Tree best, std::size_t *remaining_trials = nullptr);
Tree delay_tree(Problem const &p, Tree best, std::size_t *remaining_trials = nullptr,
                std::size_t *remaining_insertions = nullptr);
Tree scheduled_refinement(Problem const &p, Tree best, bool adaptive, std::size_t *shared_trials,
                          std::size_t *shared_insertions);
Tree refine_tree(Problem const &p, Tree best, std::size_t *trials = nullptr,
                 std::size_t *insertions = nullptr);
Tree adaptive_refine(Problem const &p, Tree best, std::size_t *trials = nullptr,
                     std::size_t *insertions = nullptr);
} // namespace cbi
