#include "io/input.hpp"
#include "io/output.hpp"
#include "solver/strategy.hpp"
#include <charconv>
#include <cmath>
#include <iostream>
#include <sstream>

namespace cbi {
struct Command {
    std::string input, output, statistics;
    SolverOptions options;
};
Expected<Command> parse_command(int argc, char const *const argv[]) {
    if (argc < 3)
        return Error{"Usage: cbi INPUT_FILE OUTPUT_FILE [--strategy current|sa_lp|hybrid] [--time "
                     "SECONDS] [--seed N] [--iterations N] [--stats FILE]"};
    Command command{argv[1], argv[2], {}, {}};
    for (int i = 3; i < argc; ++i) {
        std::string const key = argv[i];
        if (i + 1 == argc)
            return Error{"missing value for " + key};
        std::string const value = argv[++i];
        if (key == "--strategy") {
            if (value == "current")
                command.options.strategy = Strategy::current;
            else if (value == "sa_lp")
                command.options.strategy = Strategy::sa_lp;
            else if (value == "hybrid")
                command.options.strategy = Strategy::hybrid;
            else
                return Error{"unknown strategy '" + value + "'"};
        } else if (key == "--stats")
            command.statistics = value;
        else if (key == "--seed" || key == "--iterations" || key == "--repair-trials" ||
                 key == "--locality-k" || key == "--subtree-sinks" || key == "--max-lp-nodes") {
            std::uint64_t number = 0;
            auto const parsed = std::from_chars(value.data(), value.data() + value.size(), number);
            if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size())
                return Error{"invalid integer for " + key};
            if (key == "--seed")
                command.options.annealing.seed = number;
            else if (number > std::numeric_limits<std::size_t>::max())
                return Error{"integer exceeds platform range"};
            else if (key == "--iterations")
                command.options.annealing.iterations = static_cast<std::size_t>(number);
            else if (key == "--repair-trials")
                command.options.annealing.placement.repair_trials =
                    static_cast<std::size_t>(number);
            else if (key == "--locality-k")
                command.options.annealing.locality_k = static_cast<std::size_t>(number);
            else if (key == "--max-lp-nodes")
                command.options.annealing.placement.max_lp_nodes = static_cast<std::size_t>(number);
            else
                command.options.annealing.subtree_sinks = static_cast<std::size_t>(number);
        } else if (key == "--move-weights") {
            std::istringstream stream(value);
            for (std::size_t j = 0; j < move_count; ++j) {
                if (!(stream >> command.options.annealing.move_weights[j]))
                    return Error{"expected seven comma-separated move weights"};
                if (j + 1 < move_count) {
                    char comma = 0;
                    if (!(stream >> comma) || comma != ',')
                        return Error{"expected comma-separated move weights"};
                }
            }
            stream >> std::ws;
            if (!stream.eof())
                return Error{"extra move weights"};
        } else if (key == "--structural-repair" || key == "--wire-tiebreak") {
            if (value != "0" && value != "1")
                return Error{"boolean option must be 0 or 1"};
            if (key == "--structural-repair")
                command.options.annealing.structural_repair = value == "1";
            else
                command.options.annealing.placement.minimize_wire = value == "1";
        } else {
            double number = 0;
            std::istringstream stream(value);
            stream >> number;
            if (!stream || !std::isfinite(number))
                return Error{"invalid number for " + key};
            stream >> std::ws;
            if (!stream.eof())
                return Error{"invalid number for " + key};
            if (key == "--time")
                command.options.seconds = number;
            else if (key == "--current-fraction")
                command.options.current_fraction = number;
            else if (key == "--locality")
                command.options.annealing.locality_probability = number;
            else if (key == "--temperature")
                command.options.annealing.initial_temperature = number;
            else if (key == "--cooling-ratio")
                command.options.annealing.cooling_ratio = number;
            else if (key == "--lp-time")
                command.options.annealing.placement.lp_seconds = number;
            else
                return Error{"unknown option '" + key + "'"};
        }
    }
    if (command.statistics == command.output ||
        (!command.statistics.empty() && command.statistics == command.input))
        return Error{"statistics path must differ from input/output"};
    return command;
}
Expected<Metrics> run(Command const &command, TimePoint start) {
    auto const problem = read_problem(command.input);
    if (is_error(problem))
        return std::get<Error>(problem);
    auto solved = solve_with_options(std::get<Problem>(problem), command.options, start);
    if (is_error(solved))
        return std::get<Error>(solved);
    auto const &result = std::get<SolverResult>(solved);
    auto const output =
        write_output(command.output, serialize_tree(std::get<Problem>(problem), result.tree));
    if (is_error(output))
        return std::get<Error>(output);
    if (!command.statistics.empty()) {
        auto const written =
            write_output(command.statistics, statistics_json(result, command.options));
        if (is_error(written))
            return std::get<Error>(written);
    }
    return result.metrics;
}
} // namespace cbi
int main(int argc, char const *const argv[]) {
    auto const start = std::chrono::steady_clock::now();
    auto const command = cbi::parse_command(argc, argv);
    if (cbi::is_error(command)) {
        std::cerr << "cbi: " << std::get<cbi::Error>(command).message << '\n';
        return argc < 3 ? 2 : 1;
    }
    try {
        auto const result = cbi::run(std::get<cbi::Command>(command), start);
        if (cbi::is_error(result)) {
            std::cerr << "cbi: " << std::get<cbi::Error>(result).message << '\n';
            return 1;
        }
        auto const &metrics = std::get<cbi::Metrics>(result);
        std::cout << "T_max: " << metrics.maximum_arrival << ", T_min: " << metrics.minimum_arrival
                  << ", Score: " << metrics.score << '\n';
        return 0;
    } catch (std::exception const &e) {
        std::cerr << "cbi: " << e.what() << '\n';
        return 1;
    }
}
