#pragma once
#include "core/check.hpp"
#include <chrono>

namespace cbi {
enum class Quadrant { northeast, northwest, southwest, southeast };
struct Direction {
    Quadrant quadrant = Quadrant::northeast;
    int x_sign() const;
    int y_sign() const;
};
using TimePoint = std::chrono::steady_clock::time_point;
struct SearchState {
    Tree tree;
    std::vector<std::optional<BufferTypeId>> capacity;
    // Indexed by child; entry zero is unused. Every other node has one incoming edge.
    std::vector<Direction> directions;
};
[[nodiscard]] Expected<Topology>
check_search_state(Problem const &problem, SearchState const &state, bool require_capacity = true);
struct ContinuousPosition {
    double x;
    double y;
};
struct Placement {
    std::vector<ContinuousPosition> positions;
    double skew;
};
struct PlacementOptions {
    double lp_seconds = 0.2;
    bool minimize_wire = true;
    std::size_t repair_trials = 256;
    std::size_t max_lp_nodes = 4096;
};
[[nodiscard]] SearchState make_search_state(Tree tree);
[[nodiscard]] std::vector<BufferTypeId> undominated_types(Problem const &problem);
[[nodiscard]] Expected<WriteSuccess> estimate_capacity(Problem const &problem, SearchState &state,
                                                       bool relax);
[[nodiscard]] Expected<Placement> optimize_placement(Problem const &problem,
                                                     SearchState const &state,
                                                     PlacementOptions const &options,
                                                     TimePoint deadline);
[[nodiscard]] Expected<SearchState> repair_placement(Problem const &problem, SearchState state,
                                                     Placement const &placement,
                                                     PlacementOptions const &options,
                                                     TimePoint deadline);
[[nodiscard]] bool directions_hold(SearchState const &state);
} // namespace cbi
