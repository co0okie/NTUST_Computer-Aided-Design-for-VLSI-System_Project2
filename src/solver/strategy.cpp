#include "solver/strategy.hpp"
#include "construction/builder.hpp"
#include "construction/forest.hpp"
#include "solver/legacy.hpp"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace cbi {
namespace {
Expected<Tree> initial_solution(Problem const &p) {
    auto direct = initial_tree(p, false);
    for (std::size_t i = 1; i <= p.sinks.size(); ++i)
        direct[0].children.push_back(i);
    if (!is_error(evaluate_tree(p, direct)))
        return direct;
    for (bool parity : {true, false}) {
        try {
            auto candidate = Builder(p, parity).build();
            if (!is_error(evaluate_tree(p, candidate)))
                return candidate;
        } catch (std::runtime_error const &) {
        }
    }
    try {
        return solve(p);
    } catch (std::runtime_error const &e) {
        return Error{e.what(), ErrorKind::solver};
    }
}
} // namespace
Expected<SolverResult> solve_with_options(Problem p, SolverOptions const &options,
                                          TimePoint start) {
    if (!std::isfinite(options.seconds) || options.seconds <= 0 || options.seconds > 55 ||
        !std::isfinite(options.current_fraction) || options.current_fraction <= 0 ||
        options.current_fraction >= 1)
        return Error{"time must be in (0,55], current fraction in (0,1)", ErrorKind::input};
    auto const valid_options = validate_annealing_options(options.annealing);
    if (is_error(valid_options))
        return std::get<Error>(valid_options);
    auto const deadline = start + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                                      std::chrono::duration<double>(options.seconds));
    auto const reference_deadline =
        options.strategy == Strategy::hybrid
            ? start + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                          std::chrono::duration<double>(options.seconds * options.current_fraction))
            : deadline;
    p.search_deadline =
        options.strategy == Strategy::sa_lp
            ? start + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                          std::chrono::duration<double>(options.seconds * 0.4))
            : reference_deadline;
    auto initial = options.strategy == Strategy::current
                       ? Expected<Tree>{Error{"reference strategy not yet run", ErrorKind::solver}}
                       : initial_solution(p);
    std::optional<Tree> best;
    std::vector<Checkpoint> checkpoints;
    if (!is_error(initial)) {
        best = std::get<Tree>(std::move(initial));
        checkpoints.push_back(
            {std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count(),
             std::get<Metrics>(evaluate_tree(p, *best))});
    }
    if (options.strategy != Strategy::sa_lp) {
        try {
            auto candidate = solve(p);
            if (!best || validate_tree(p, candidate).score <= validate_tree(p, *best).score)
                best = std::move(candidate);
        } catch (std::runtime_error const &) {
        }
    }
    if (!best) {
        auto fallback = initial_solution(p);
        if (is_error(fallback))
            return std::get<Error>(fallback);
        best = std::get<Tree>(std::move(fallback));
    }
    SolverResult result{std::move(*best), {}, Termination::completed, {}, {}, 0};
    result.metrics = std::get<Metrics>(evaluate_tree(p, result.tree));
    auto const initial_seconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    result.checkpoints = std::move(checkpoints);
    result.checkpoints.push_back({initial_seconds, result.metrics});
    auto const lower_bound = score_lower_bound(p);
    if (lower_bound && result.metrics.score == *lower_bound)
        result.termination = Termination::lower_bound;
    else if (options.strategy != Strategy::current &&
             (p.dimensions.x > (Integer{1} << 40) || p.dimensions.y > (Integer{1} << 40) ||
              result.tree.size() > options.annealing.placement.max_lp_nodes))
        result.termination = Termination::numerical_domain;
    else if (options.strategy != Strategy::current && std::chrono::steady_clock::now() < deadline) {
        p.search_deadline = deadline;
        auto const sa_start = std::chrono::steady_clock::now();
        auto searched = anneal(p, result.tree, options.annealing, deadline);
        if (is_error(searched))
            return std::get<Error>(searched);
        auto sa = std::get<AnnealingResult>(std::move(searched));
        result.statistics = sa.statistics;
        for (auto point : sa.checkpoints) {
            point.seconds += std::chrono::duration<double>(sa_start - start).count();
            result.checkpoints.push_back(point);
        }
        if (sa.metrics.score < result.metrics.score) {
            result.tree = std::move(sa.best.tree);
            result.metrics = sa.metrics;
        }
        if (lower_bound && result.metrics.score == *lower_bound)
            result.termination = Termination::lower_bound;
        else if (std::chrono::steady_clock::now() >= deadline)
            result.termination = Termination::deadline;
        else
            result.termination = options.annealing.iterations ? Termination::iteration_budget
                                                              : Termination::completed;
    } else if (std::chrono::steady_clock::now() >= deadline)
        result.termination = Termination::deadline;
    result.seconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    auto const validated = evaluate_tree(p, result.tree);
    if (is_error(validated))
        return std::get<Error>(validated);
    result.metrics = std::get<Metrics>(validated);
    return result;
}
std::string statistics_json(SolverResult const &r, SolverOptions const &options) {
    static constexpr char const *names[]{"move_sink",     "swap_sinks",       "insert_buffer",
                                         "delete_buffer", "change_direction", "change_type",
                                         "move_subtree"};
    std::ostringstream out;
    out << std::setprecision(12);
    out << "{\n\"strategy\":\""
        << (options.strategy == Strategy::current ? "current"
            : options.strategy == Strategy::sa_lp ? "sa_lp"
                                                  : "hybrid")
        << "\",\"seed\":" << options.annealing.seed << ",\"seconds\":" << r.seconds
        << ",\"termination\":" << static_cast<int>(r.termination)
        << ",\"score\":" << r.metrics.score
        << ",\"skew\":" << r.metrics.maximum_arrival - r.metrics.minimum_arrival
        << ",\"lp_calls\":" << r.statistics.lp_calls
        << ",\"lp_feasible\":" << r.statistics.lp_feasible
        << ",\"lp_seconds\":" << r.statistics.lp_seconds
        << ",\"repair_calls\":" << r.statistics.repair_calls
        << ",\"repair_feasible\":" << r.statistics.repair_feasible
        << ",\"repair_seconds\":" << r.statistics.repair_seconds << ",\n\"moves\":[";
    for (std::size_t i = 0; i < move_count; ++i) {
        auto const &s = r.statistics.moves[i];
        if (i)
            out << ',';
        out << "{\"name\":\"" << names[i] << "\",\"attempts\":" << s.attempts
            << ",\"feasible\":" << s.feasible << ",\"accepted\":" << s.accepted
            << ",\"improvements\":" << s.improvements << ",\"seconds\":" << s.seconds << '}';
    }
    out << "],\n\"checkpoints\":[";
    for (std::size_t i = 0; i < r.checkpoints.size(); ++i) {
        auto const &p = r.checkpoints[i];
        if (i)
            out << ',';
        out << "{\"seconds\":" << p.seconds << ",\"score\":" << p.metrics.score
            << ",\"skew\":" << p.metrics.maximum_arrival - p.metrics.minimum_arrival << '}';
    }
    out << "]\n}\n";
    return out.str();
}
} // namespace cbi
