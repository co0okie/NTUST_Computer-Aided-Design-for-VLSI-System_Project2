#pragma once
#include "model.hpp"
namespace cbi {
inline bool search_expired(Problem const &p) {
    return p.search_deadline && std::chrono::steady_clock::now() >= *p.search_deadline;
}
} // namespace cbi
