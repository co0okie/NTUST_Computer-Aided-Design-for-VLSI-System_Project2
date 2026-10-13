#pragma once
#include "search/annealing.hpp"
namespace cbi {
enum class Strategy { current, sa_lp, hybrid };
enum class Termination { completed, deadline, lower_bound, iteration_budget, numerical_domain };
struct SolverOptions {
    Strategy strategy = Strategy::hybrid;
    double seconds = 55;
    double current_fraction = 0.4;
    AnnealingOptions annealing;
};
struct SolverResult {
    Tree tree;
    Metrics metrics;
    Termination termination = Termination::completed;
    SearchStatistics statistics;
    std::vector<Checkpoint> checkpoints;
    double seconds = 0;
};
[[nodiscard]] Expected<SolverResult>
solve_with_options(Problem problem, SolverOptions const &options,
                   TimePoint start = std::chrono::steady_clock::now());
std::string statistics_json(SolverResult const &result, SolverOptions const &options);
} // namespace cbi
