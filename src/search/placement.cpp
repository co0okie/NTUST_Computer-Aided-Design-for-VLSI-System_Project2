#include "search/placement.hpp"
#include <Highs.h>
#include <algorithm>
#include <cmath>
#include <map>
#include <unordered_set>

namespace cbi {
int Direction::x_sign() const {
    return quadrant == Quadrant::northeast || quadrant == Quadrant::southeast ? 1 : -1;
}
int Direction::y_sign() const {
    return quadrant == Quadrant::northeast || quadrant == Quadrant::northwest ? 1 : -1;
}
SearchState make_search_state(Tree tree) {
    SearchState state{std::move(tree), {}, {}};
    state.capacity.resize(state.tree.size());
    state.directions.resize(state.tree.size());
    for (std::size_t u = 0; u < state.tree.size(); ++u) {
        if (state.tree[u].is_buffer())
            state.capacity[u] = state.tree[u].buffer_type;
        for (auto v : state.tree[u].children) {
            if (v >= state.tree.size())
                continue;
            auto const a = state.tree[u].position, b = state.tree[v].position;
            auto const q = b.x >= a.x ? (b.y >= a.y ? Quadrant::northeast : Quadrant::southeast)
                                      : (b.y >= a.y ? Quadrant::northwest : Quadrant::southwest);
            state.directions[v] = Direction{q};
        }
    }
    return state;
}
Expected<Topology> check_search_state(Problem const &p, SearchState const &state,
                                      bool require_capacity) {
    auto const topology = check_topology(p, state.tree);
    if (is_error(topology))
        return std::get<Error>(topology);
    if (state.capacity.size() != state.tree.size() || state.directions.size() != state.tree.size())
        return Error{"search state dimensions disagree", ErrorKind::structure};
    for (std::size_t u = 0; u < state.tree.size(); ++u) {
        if (static_cast<unsigned>(state.directions[u].quadrant) >= 4)
            return Error{"invalid edge direction", ErrorKind::structure};
        auto const cap = state.capacity[u];
        if ((!state.tree[u].is_buffer() && cap) ||
            (require_capacity && state.tree[u].is_buffer() && !cap) ||
            (cap && cap->value >= p.buffer_library.size()))
            return Error{"invalid capacity choice", ErrorKind::structure};
    }
    return topology;
}
std::vector<BufferTypeId> undominated_types(Problem const &p) {
    std::vector<BufferTypeId> types;
    for (std::size_t b = 0; b < p.buffer_library.size(); ++b) {
        bool dominated = false;
        auto const &tb = p.buffer_library[b];
        for (std::size_t a = 0; a < p.buffer_library.size(); ++a) {
            if (a == b)
                continue;
            auto const &ta = p.buffer_library[a];
            if (ta.fanout >= tb.fanout && ta.length >= tb.length && ta.cost <= tb.cost &&
                (ta.fanout > tb.fanout || ta.length > tb.length || ta.cost < tb.cost || a < b)) {
                dominated = true;
                break;
            }
        }
        if (!dominated)
            types.push_back(BufferTypeId{b});
    }
    return types;
}
Expected<WriteSuccess> estimate_capacity(Problem const &p, SearchState &state, bool relax) {
    auto const topology = check_search_state(p, state, false);
    if (is_error(topology))
        return std::get<Error>(topology);
    if (state.capacity.size() != state.tree.size() || state.directions.size() != state.tree.size())
        return Error{"search state dimensions disagree", ErrorKind::structure};
    if (state.tree[0].children.size() > static_cast<std::uint64_t>(p.source_limits.fanout))
        return Error{"source fanout exceeds limit", ErrorKind::capacity};
    for (std::size_t u = p.sinks.size() + 1; u < state.tree.size(); ++u) {
        auto const count = state.tree[u].children.size();
        std::vector<Integer> xs, ys;
        for (auto v : state.tree[u].children) {
            xs.push_back(state.tree[v].position.x);
            ys.push_back(state.tree[v].position.y);
        }
        std::sort(xs.begin(), xs.end());
        std::sort(ys.begin(), ys.end());
        Position const median{xs[count / 2], ys[count / 2]};
        Integer estimated = 0;
        bool overflow = false;
        for (auto v : state.tree[u].children) {
            auto const d = checked_distance(median, state.tree[v].position);
            auto const sum = d ? checked_add(estimated, *d) : std::nullopt;
            if (!sum) {
                overflow = true;
                break;
            }
            estimated = *sum;
        }
        auto const current = state.capacity[u];
        if (!relax && current && current->value < p.buffer_library.size()) {
            auto const &t = p.buffer_library[current->value];
            // Geometry estimates must not eliminate a feasible re-placement.
            if (count <= static_cast<std::uint64_t>(t.fanout) &&
                (!overflow && estimated <= t.length))
                continue;
        }
        std::optional<BufferTypeId> chosen;
        for (std::size_t i = 0; i < p.buffer_library.size(); ++i) {
            auto const &t = p.buffer_library[i];
            if (count > static_cast<std::uint64_t>(t.fanout))
                continue;
            if (!chosen) {
                chosen = BufferTypeId{i};
                continue;
            }
            auto const &best = p.buffer_library[chosen->value];
            if (relax) {
                if (t.length > best.length || (t.length == best.length && t.cost < best.cost))
                    chosen = BufferTypeId{i};
            } else {
                bool const fits = !overflow && t.length >= estimated;
                bool const best_fits = !overflow && best.length >= estimated;
                if ((fits && !best_fits) ||
                    (fits == best_fits && (fits ? t.cost < best.cost : t.length > best.length)))
                    chosen = BufferTypeId{i};
            }
        }
        if (!chosen)
            return Error{"no type supports topology fanout", ErrorKind::capacity};
        state.capacity[u] = chosen;
    }
    return WriteSuccess{};
}
namespace {
struct Expression {
    std::map<HighsInt, double> terms;
    double constant = 0;
    void term(HighsInt column, double value) { terms[column] += value; }
    void append(Expression const &other, double factor = 1) {
        constant += other.constant * factor;
        for (auto const &term : other.terms)
            terms[term.first] += term.second * factor;
    }
};
struct Model {
    HighsLp lp;
    Model() {
        lp.a_matrix_.format_ = MatrixFormat::kRowwise;
        lp.a_matrix_.start_.assign(1, 0);
    }
    HighsInt variable(double lower, double upper, double cost = 0) {
        auto const index = lp.num_col_++;
        lp.col_lower_.push_back(lower);
        lp.col_upper_.push_back(upper);
        lp.col_cost_.push_back(cost);
        return index;
    }
    void row(Expression const &expression, double lower, double upper) {
        ++lp.num_row_;
        lp.row_lower_.push_back(lower - expression.constant);
        lp.row_upper_.push_back(upper - expression.constant);
        for (auto const &term : expression.terms)
            if (term.second != 0) {
                lp.a_matrix_.index_.push_back(term.first);
                lp.a_matrix_.value_.push_back(term.second);
            }
        lp.a_matrix_.start_.push_back(static_cast<HighsInt>(lp.a_matrix_.index_.size()));
    }
};
} // namespace
Expected<Placement> optimize_placement(Problem const &p, SearchState const &state,
                                       PlacementOptions const &options, TimePoint deadline) {
    if (!std::isfinite(options.lp_seconds) || options.lp_seconds <= 0)
        return Error{"invalid LP time budget", ErrorKind::input};
    auto const topology = check_search_state(p, state);
    if (is_error(topology))
        return std::get<Error>(topology);
    if (state.capacity.size() != state.tree.size() || state.directions.size() != state.tree.size())
        return Error{"search state dimensions disagree", ErrorKind::structure};
    if (state.tree.size() > options.max_lp_nodes)
        return Error{"LP node budget exceeded", ErrorKind::numerical};
    constexpr Integer safe = Integer{1} << 40;
    if (p.dimensions.x > safe || p.dimensions.y > safe || p.source_limits.length > safe ||
        p.dimensions.x <= 1 || p.dimensions.y <= 1)
        return Error{"geometry exceeds reliable LP domain", ErrorKind::numerical};
    auto const seconds = std::min(
        options.lp_seconds,
        std::chrono::duration<double>(deadline - std::chrono::steady_clock::now()).count());
    if (seconds <= 0)
        return Error{"LP deadline reached", ErrorKind::timeout};
    auto const n = state.tree.size();
    Position origin = p.source;
    for (auto point : p.sinks) {
        origin.x = std::min(origin.x, point.x);
        origin.y = std::min(origin.y, point.y);
    }
    Model model;
    std::vector<HighsInt> xs(n, -1), ys(n, -1), arrival(n);
    for (std::size_t u = 0; u < n; ++u) {
        if (state.tree[u].is_buffer()) {
            auto const cap = state.capacity[u];
            if (!cap || cap->value >= p.buffer_library.size())
                return Error{"missing capacity", ErrorKind::structure};
            if (p.buffer_library[cap->value].length > safe)
                return Error{"capacity exceeds reliable LP domain", ErrorKind::numerical};
            xs[u] = model.variable(static_cast<double>(1 - origin.x),
                                   static_cast<double>(p.dimensions.x - 1 - origin.x));
            ys[u] = model.variable(static_cast<double>(1 - origin.y),
                                   static_cast<double>(p.dimensions.y - 1 - origin.y));
        }
        arrival[u] = model.variable(0, u == 0 ? 0 : kHighsInf);
    }
    auto const minimum = model.variable(0, kHighsInf, -1);
    auto const maximum = model.variable(0, kHighsInf, 1);
    auto coordinate = [&](std::size_t u, bool x, int sign) {
        Expression expression;
        auto const col = x ? xs[u] : ys[u];
        if (col >= 0)
            expression.term(col, sign);
        else
            expression.constant = static_cast<double>((x ? state.tree[u].position.x - origin.x
                                                         : state.tree[u].position.y - origin.y)) *
                                  sign;
        return expression;
    };
    Expression total_wire;
    for (std::size_t u = 0; u < n; ++u) {
        if (std::chrono::steady_clock::now() >= deadline)
            return Error{"LP model deadline reached", ErrorKind::timeout};
        auto const limit =
            u == 0 ? p.source_limits.length
                   : (state.capacity[u] ? p.buffer_library[state.capacity[u]->value].length : 0);
        auto const fanout =
            u == 0 ? p.source_limits.fanout
                   : (state.capacity[u] ? p.buffer_library[state.capacity[u]->value].fanout : 0);
        if (state.tree[u].children.size() > static_cast<std::uint64_t>(fanout))
            return Error{"LP fanout violation", ErrorKind::capacity};
        Expression outgoing;
        for (auto v : state.tree[u].children) {
            Expression length;
            for (bool x : {true, false}) {
                auto const sign = x ? state.directions[v].x_sign() : state.directions[v].y_sign();
                auto axis = coordinate(v, x, sign);
                axis.append(coordinate(u, x, sign), -1);
                model.row(axis, 0, kHighsInf);
                length.append(axis);
            }
            Expression time;
            time.term(arrival[v], 1);
            time.term(arrival[u], -1);
            time.append(length, -1);
            model.row(time, 0, 0);
            outgoing.append(length);
        }
        if (!state.tree[u].children.empty())
            model.row(outgoing, -kHighsInf, static_cast<double>(limit));
        total_wire.append(outgoing);
    }
    for (std::size_t v = 1; v <= p.sinks.size(); ++v) {
        Expression low;
        low.term(arrival[v], 1);
        low.term(minimum, -1);
        model.row(low, 0, kHighsInf);
        Expression high;
        high.term(maximum, 1);
        high.term(arrival[v], -1);
        model.row(high, 0, kHighsInf);
    }
    Highs highs;
    highs.setOptionValue("output_flag", false);
    highs.setOptionValue("threads", 1);
    highs.setOptionValue("solver", "simplex");
    highs.setOptionValue("time_limit", seconds);
    if (highs.passModel(model.lp) == HighsStatus::kError || highs.run() == HighsStatus::kError)
        return Error{"LP solver error", ErrorKind::solver};
    auto const status = highs.getModelStatus();
    if (status == HighsModelStatus::kInfeasible)
        return Error{"LP infeasible", ErrorKind::capacity};
    if (status != HighsModelStatus::kOptimal || !highs.getSolution().value_valid)
        return Error{"LP stopped without optimum", ErrorKind::timeout};
    auto values = highs.getSolution().col_value;
    auto const skew =
        values[static_cast<std::size_t>(maximum)] - values[static_cast<std::size_t>(minimum)];
    // Lexicographic wire minimization reduces gratuitous displacement/rounding failures.
    auto const remaining = std::min(
        seconds / 2,
        std::chrono::duration<double>(deadline - std::chrono::steady_clock::now()).count());
    if (options.minimize_wire && remaining > 0.001) {
        Expression bound;
        bound.term(maximum, 1);
        bound.term(minimum, -1);
        model.row(bound, -kHighsInf, skew + 1e-7);
        std::fill(model.lp.col_cost_.begin(), model.lp.col_cost_.end(), 0);
        for (auto const &term : total_wire.terms)
            model.lp.col_cost_[static_cast<std::size_t>(term.first)] = term.second;
        highs.setOptionValue("time_limit", remaining);
        if (highs.passModel(model.lp) != HighsStatus::kError &&
            highs.run() != HighsStatus::kError &&
            highs.getModelStatus() == HighsModelStatus::kOptimal && highs.getSolution().value_valid)
            values = highs.getSolution().col_value;
    }
    Placement result{{}, skew};
    result.positions.reserve(n);
    for (std::size_t u = 0; u < n; ++u) {
        auto const x =
            xs[u] < 0 ? static_cast<double>(state.tree[u].position.x)
                      : values[static_cast<std::size_t>(xs[u])] + static_cast<double>(origin.x);
        auto const y =
            ys[u] < 0 ? static_cast<double>(state.tree[u].position.y)
                      : values[static_cast<std::size_t>(ys[u])] + static_cast<double>(origin.y);
        if (!std::isfinite(x) || !std::isfinite(y))
            return Error{"nonfinite LP solution", ErrorKind::numerical};
        result.positions.push_back({x, y});
    }
    return result;
}
bool directions_hold(SearchState const &state) {
    if (state.directions.size() != state.tree.size())
        return false;
    for (std::size_t u = 0; u < state.tree.size(); ++u)
        for (auto v : state.tree[u].children) {
            if (v >= state.tree.size())
                return false;
            auto const a = state.tree[u].position, b = state.tree[v].position;
            auto const &d = state.directions[v];
            if (static_cast<unsigned>(d.quadrant) >= 4)
                return false;
            if ((d.x_sign() > 0 ? b.x < a.x : b.x > a.x) ||
                (d.y_sign() > 0 ? b.y < a.y : b.y > a.y))
                return false;
        }
    return true;
}
namespace {
struct PointHash {
    std::size_t operator()(std::pair<Integer, Integer> const &p) const {
        auto const a = std::hash<Integer>{}(p.first), b = std::hash<Integer>{}(p.second);
        return a ^ (b + std::size_t{0x9e3779b9} + (a << 6) + (a >> 2));
    }
};
struct Penalty {
    std::size_t violations = 0;
    long double excess = 0;
    bool operator<(Penalty const &other) const {
        return violations != other.violations ? violations < other.violations
                                              : excess < other.excess;
    }
};
Penalty penalty(Problem const &p, SearchState const &s) {
    Penalty result;
    std::unordered_set<std::pair<Integer, Integer>, PointHash> occupied;
    for (auto const &node : s.tree) {
        if (!occupied.emplace(node.position.x, node.position.y).second)
            ++result.violations;
        if (node.is_buffer() &&
            (node.position.x <= 0 || node.position.y <= 0 || node.position.x >= p.dimensions.x ||
             node.position.y >= p.dimensions.y))
            ++result.violations;
    }
    for (std::size_t u = 0; u < s.tree.size(); ++u) {
        Integer wire = 0;
        for (auto v : s.tree[u].children) {
            auto const a = s.tree[u].position, b = s.tree[v].position;
            auto const &direction = s.directions[v];
            if ((direction.x_sign() > 0 ? b.x < a.x : b.x > a.x) ||
                (direction.y_sign() > 0 ? b.y < a.y : b.y > a.y))
                ++result.violations;
            auto const d = checked_distance(a, b);
            auto const sum = d ? checked_add(wire, *d) : std::nullopt;
            if (!sum) {
                ++result.violations;
                result.excess += 1e30L;
            } else
                wire = *sum;
        }
        auto const type = u == 0 ? std::optional<BufferTypeId>{}
                                 : feasible_type(p, s.tree[u].children.size(), wire);
        if (u == 0 && wire > p.source_limits.length) {
            ++result.violations;
            result.excess += static_cast<long double>(wire - p.source_limits.length);
        }
        if (s.tree[u].is_buffer() && !type) {
            ++result.violations;
            Integer maximum = 0;
            for (auto const &t : p.buffer_library)
                if (s.tree[u].children.size() <= static_cast<std::uint64_t>(t.fanout))
                    maximum = std::max(maximum, t.length);
            result.excess += static_cast<long double>(std::max(Integer{0}, wire - maximum));
        }
    }
    return result;
}
} // namespace
Expected<SearchState> repair_placement(Problem const &p, SearchState state,
                                       Placement const &placement, PlacementOptions const &options,
                                       TimePoint deadline) {
    if (placement.positions.size() != state.tree.size())
        return Error{"placement size mismatch", ErrorKind::structure};
    auto const topology = check_search_state(p, state);
    if (is_error(topology))
        return std::get<Error>(topology);
    auto const original = state.tree;
    constexpr double safe = static_cast<double>(Integer{1} << 40);
    for (std::size_t u = p.sinks.size() + 1; u < state.tree.size(); ++u) {
        auto const point = placement.positions[u];
        if (!std::isfinite(point.x) || !std::isfinite(point.y) || point.x < -safe ||
            point.y < -safe || point.x > safe || point.y > safe)
            return Error{"unsafe coordinate rounding", ErrorKind::numerical};
        state.tree[u].position = {static_cast<Integer>(std::llround(point.x)),
                                  static_cast<Integer>(std::llround(point.y))};
    }
    auto finish = [&]() -> std::optional<SearchState> {
        if (!directions_hold(state) || is_error(normalize_types(p, state.tree)) ||
            is_error(evaluate_tree(p, state.tree)))
            return std::nullopt;
        for (std::size_t u = p.sinks.size() + 1; u < state.tree.size(); ++u) {
            Integer wire = 0;
            for (auto v : state.tree[u].children)
                wire += *checked_distance(state.tree[u].position, state.tree[v].position);
            auto const cap = state.capacity[u];
            if (!cap || wire > p.buffer_library[cap->value].length ||
                state.tree[u].children.size() >
                    static_cast<std::uint64_t>(p.buffer_library[cap->value].fanout))
                state.capacity[u] = state.tree[u].buffer_type;
        }
        return state;
    };
    if (auto result = finish())
        return std::move(*result);
    std::size_t trials = 0;
    for (Integer radius : {Integer{1}, Integer{2}, Integer{4}}) {
        for (unsigned pass = 0; pass < 3; ++pass) {
            bool improved = false;
            for (std::size_t u = p.sinks.size() + 1; u < state.tree.size(); ++u) {
                if (trials >= options.repair_trials || std::chrono::steady_clock::now() >= deadline)
                    break;
                auto const point = state.tree[u].position;
                auto best_point = point;
                auto best_penalty = penalty(p, state);
                std::vector<Position> offers{original[u].position};
                for (Integer dx = -radius; dx <= radius; ++dx)
                    for (Integer dy = -radius; dy <= radius; ++dy)
                        if (std::abs(dx) == radius || std::abs(dy) == radius)
                            offers.push_back({point.x + dx, point.y + dy});
                for (auto offer : offers) {
                    if (trials >= options.repair_trials ||
                        std::chrono::steady_clock::now() >= deadline)
                        break;
                    ++trials;
                    if (offer.x <= 0 || offer.y <= 0 || offer.x >= p.dimensions.x ||
                        offer.y >= p.dimensions.y)
                        continue;
                    state.tree[u].position = offer;
                    auto const candidate = penalty(p, state);
                    if (candidate < best_penalty) {
                        best_penalty = candidate;
                        best_point = offer;
                    }
                }
                state.tree[u].position = best_point;
                improved = improved || best_point.x != point.x || best_point.y != point.y;
                if (!best_penalty.violations)
                    if (auto result = finish())
                        return std::move(*result);
            }
            if (!improved)
                break;
        }
    }
    return Error{"integer repair budget exhausted", ErrorKind::geometry};
}
} // namespace cbi
