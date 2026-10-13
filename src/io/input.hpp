#pragma once
#include "model.hpp"
namespace cbi {
Expected<Problem> read_problem(std::string const &path);
}
