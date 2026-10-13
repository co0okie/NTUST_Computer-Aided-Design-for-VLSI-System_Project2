#pragma once
#include "search/placement.hpp"
#include <array>
#include <random>

namespace cbi {
enum class AnnealingMove {
    move_sink,
    swap_sinks,
    insert_buffer,
    delete_buffer,
    change_direction,
    change_type,
    move_subtree
};
inline constexpr std::size_t move_count = 7;
struct AnnealingOptions {
    std::uint64_t seed = 20261013;
    std::size_t iterations = 0;
    std::array<double, move_count> move_weights{25, 15, 15, 10, 20, 10, 5};
    double locality_probability = 0.85;
    std::size_t locality_k = 5;
    std::size_t subtree_sinks = 4;
    double initial_temperature = 0; // zero: calibrate from positive exact-score differences
    double cooling_ratio = 0.01;
    bool structural_repair = true;
    PlacementOptions placement;
};
struct MoveStatistics {
    std::size_t attempts = 0, feasible = 0, accepted = 0, improvements = 0;
    double seconds = 0;
};
struct SearchStatistics {
    std::array<MoveStatistics, move_count> moves{};
    std::size_t lp_calls = 0, lp_feasible = 0, repair_calls = 0, repair_feasible = 0;
    double lp_seconds = 0, repair_seconds = 0;
};
struct Checkpoint {
    double seconds;
    Metrics metrics;
};
struct AnnealingResult {
    SearchState best;
    Metrics metrics;
    SearchStatistics statistics;
    std::vector<Checkpoint> checkpoints;
};
[[nodiscard]] Expected<WriteSuccess> validate_annealing_options(AnnealingOptions const &options);
[[nodiscard]] Expected<SearchState> perturb_state(Problem const &problem, SearchState state,
                                                  AnnealingMove move,
                                                  AnnealingOptions const &options,
                                                  std::mt19937_64 &rng);
[[nodiscard]] Expected<AnnealingResult> anneal(Problem const &problem, Tree initial,
                                               AnnealingOptions const &options, TimePoint deadline);
} // namespace cbi
