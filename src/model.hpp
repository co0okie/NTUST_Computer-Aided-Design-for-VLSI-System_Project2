#pragma once

#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace cbi {

enum class ErrorKind { input, structure, geometry, capacity, overflow, timeout, numerical, solver };

struct Error {
    std::string message;
    ErrorKind kind = ErrorKind::input;
};

template <typename T>
using Expected = std::variant<T, Error>;

template <typename T>
bool is_error(Expected<T> const& value) {
    return std::holds_alternative<Error>(value);
}

struct Position {
    std::int64_t x;
    std::int64_t y;
};

struct SourceLimits {
    std::int64_t fanout;
    std::int64_t length;
};

struct Dimensions {
    std::int64_t x;
    std::int64_t y;
};

struct BufferType {
    std::string name;
    std::int64_t fanout;
    std::int64_t length;
    std::int64_t cost;
};

struct Problem {
    SourceLimits source_limits;
    Dimensions dimensions;
    std::vector<BufferType> buffer_library;
    std::int64_t skew_weight;
    Position source;
    std::vector<Position> sinks;
};

struct Metrics {
    std::int64_t maximum_arrival;
    std::int64_t minimum_arrival;
    std::int64_t score;
};

struct WriteSuccess {};

} // namespace cbi
