#include "io/input.hpp"
#include "io/output.hpp"
#include "solver/legacy.hpp"
#include <iostream>

namespace cbi {
Expected<Metrics> run(std::string const& input_path,
                      std::string const& output_path) {
    auto const problem = read_problem(input_path);
    if (is_error(problem)) {
        return std::get<Error>(problem);
    }
    auto const& parsed_problem = std::get<Problem>(problem);
    try {
        auto const tree = solve(parsed_problem);
        auto const metrics = validate_tree(parsed_problem, tree);
        auto const write_result = write_output(output_path, serialize_tree(parsed_problem, tree));
        if (is_error(write_result)) {
            return std::get<Error>(write_result);
        }
        return metrics;
    } catch (std::runtime_error const& error) {
        return Error{error.what()};
    }
}

}  // namespace cbi

int main(int argc, char const* const argv[]) {
    if (argc != 3) {
        std::cerr << "Usage: cbi INPUT_FILE OUTPUT_FILE\n";
        return 2;
    }

    auto const result = cbi::run(argv[1], argv[2]);
    if (cbi::is_error(result)) {
        std::cerr << "cbi: " << std::get<cbi::Error>(result).message << '\n';
        return 1;
    }

    auto const& metrics = std::get<cbi::Metrics>(result);
    std::cout << "T_max: " << metrics.maximum_arrival
              << ", T_min: " << metrics.minimum_arrival
              << ", Score: " << metrics.score << '\n';
    return 0;
}
