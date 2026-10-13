#include <algorithm>
#include <charconv>
#include <cstdint>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <set>
#include <stdexcept>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

#include "io/input.hpp"
namespace cbi {
struct LogicalLine {
    std::size_t number;
    std::vector<std::string> words;
};

std::string line_error(LogicalLine const& line, std::string message) {
    return "line " + std::to_string(line.number) + ": " + std::move(message);
}

Expected<std::int64_t> parse_integer(std::string const& text,
                                     LogicalLine const& line,
                                     std::string_view field_name) {
    std::int64_t value = 0;
    auto const* const begin = text.data();
    auto const* const end = begin + text.size();
    auto const result = std::from_chars(begin, end, value);
    if (result.ec != std::errc{} || result.ptr != end) {
        return Error{line_error(
            line, "invalid integer for " + std::string(field_name) + ": '" +
                      text + "'")};
    }
    return value;
}

Expected<std::vector<LogicalLine>> read_logical_lines(
    std::string const& input_path) {
    std::ifstream input{input_path};
    if (!input) {
        return Error{"cannot open input file '" + input_path + "'"};
    }

    std::vector<LogicalLine> lines;
    std::string text;
    std::size_t line_number = 0;
    while (std::getline(input, text)) {
        ++line_number;
        auto const comment = text.find('#');
        if (comment != std::string::npos) {
            text.erase(comment);
        }

        std::istringstream stream{text};
        std::vector<std::string> words;
        std::string word;
        while (stream >> word) {
            words.push_back(std::move(word));
        }
        if (!words.empty()) {
            lines.push_back(LogicalLine{line_number, std::move(words)});
        }
    }
    if (input.bad()) {
        return Error{"failed while reading input file '" + input_path + "'"};
    }
    return lines;
}

class Cursor final {
public:
    explicit Cursor(std::vector<LogicalLine> const& lines) : lines_{lines} {}

    std::optional<std::reference_wrapper<LogicalLine const>> next() {
        if (index_ == lines_.size()) {
            return std::nullopt;
        }
        return std::cref(lines_[index_++]);
    }

    bool at_end() const { return index_ == lines_.size(); }

private:
    std::vector<LogicalLine> const& lines_;
    std::size_t index_ = 0;
};

Expected<LogicalLine> require_line(Cursor& cursor, std::string_view expected) {
    auto const line = cursor.next();
    if (!line.has_value()) {
        return Error{"unexpected end of file; expected " +
                     std::string(expected)};
    }
    return line->get();
}

Expected<LogicalLine> require_words(Cursor& cursor,
                                    std::vector<std::string_view> const& words) {
    std::string expected;
    for (auto const word : words) {
        if (!expected.empty()) {
            expected += ' ';
        }
        expected += word;
    }

    auto const candidate = require_line(cursor, expected);
    if (is_error(candidate)) {
        return std::get<Error>(candidate);
    }
    auto const& line = std::get<LogicalLine>(candidate);
    if (line.words.size() != words.size() ||
        !std::equal(words.begin(), words.end(), line.words.begin())) {
        return Error{line_error(line, "expected '" + expected + "'")};
    }
    return line;
}

Expected<std::int64_t> require_positive_integer(std::string const& text,
                                                LogicalLine const& line,
                                                std::string_view field_name) {
    auto const parsed = parse_integer(text, line, field_name);
    if (is_error(parsed)) {
        return std::get<Error>(parsed);
    }
    auto const value = std::get<std::int64_t>(parsed);
    if (value <= 0) {
        return Error{line_error(line, std::string(field_name) +
                                         " must be positive")};
    }
    return value;
}

Expected<Problem> parse_problem(std::vector<LogicalLine> const& lines) {
    Cursor cursor{lines};
    auto const limit_header = require_words(cursor, {".limit"});
    if (is_error(limit_header)) {
        return std::get<Error>(limit_header);
    }

    std::optional<std::int64_t> source_fanout;
    std::optional<std::int64_t> source_length;
    std::optional<std::int64_t> dimension_x;
    std::optional<std::int64_t> dimension_y;
    while (true) {
        auto const candidate = require_line(cursor, "a .limit entry or .e");
        if (is_error(candidate)) {
            return std::get<Error>(candidate);
        }
        auto const& line = std::get<LogicalLine>(candidate);
        if (line.words == std::vector<std::string>{".e"}) {
            break;
        }
        if (line.words.size() != 2) {
            return Error{line_error(line, "expected a key and an integer")};
        }

        auto const value = require_positive_integer(line.words[1], line,
                                                    line.words[0]);
        if (is_error(value)) {
            return std::get<Error>(value);
        }
        auto const parsed = std::get<std::int64_t>(value);
        auto* destination = [&]() -> std::optional<std::int64_t>* {
            if (line.words[0] == "fanout") {
                return &source_fanout;
            }
            if (line.words[0] == "length") {
                return &source_length;
            }
            if (line.words[0] == ".dimx") {
                return &dimension_x;
            }
            if (line.words[0] == ".dimy") {
                return &dimension_y;
            }
            return nullptr;
        }();
        if (destination == nullptr) {
            return Error{line_error(line, "unknown .limit key '" +
                                              line.words[0] + "'")};
        }
        if (destination->has_value()) {
            return Error{line_error(line, "duplicate .limit key '" +
                                              line.words[0] + "'")};
        }
        *destination = parsed;
    }
    if (!source_fanout || !source_length || !dimension_x || !dimension_y) {
        return Error{"the .limit section must define fanout, length, .dimx, "
                     "and .dimy exactly once"};
    }

    auto const library_header = require_line(cursor, ".buflib <count>");
    if (is_error(library_header)) {
        return std::get<Error>(library_header);
    }
    auto const& library_line = std::get<LogicalLine>(library_header);
    if (library_line.words.size() != 2 ||
        library_line.words[0] != ".buflib") {
        return Error{line_error(library_line, "expected '.buflib <count>'")};
    }
    auto const library_count_result = require_positive_integer(
        library_line.words[1], library_line, "buffer library count");
    if (is_error(library_count_result)) {
        return std::get<Error>(library_count_result);
    }
    auto const library_count = std::get<std::int64_t>(library_count_result);

    std::vector<BufferType> library;
    std::unordered_set<std::string> type_names;
    for (std::int64_t index = 0; index < library_count; ++index) {
        auto const candidate = require_line(cursor, "a buffer library entry");
        if (is_error(candidate)) {
            return std::get<Error>(candidate);
        }
        auto const& line = std::get<LogicalLine>(candidate);
        if (line.words.size() != 4) {
            return Error{line_error(
                line, "expected '<type> <fanout> <length> <cost>'")};
        }
        if (!type_names.insert(line.words[0]).second) {
            return Error{line_error(line, "duplicate buffer type '" +
                                              line.words[0] + "'")};
        }
        auto const fanout = require_positive_integer(line.words[1], line,
                                                     "buffer fanout");
        auto const length = require_positive_integer(line.words[2], line,
                                                     "buffer length");
        auto const cost = require_positive_integer(line.words[3], line,
                                                   "buffer cost");
        if (is_error(fanout)) {
            return std::get<Error>(fanout);
        }
        if (is_error(length)) {
            return std::get<Error>(length);
        }
        if (is_error(cost)) {
            return std::get<Error>(cost);
        }
        library.push_back(BufferType{line.words[0],
                                     std::get<std::int64_t>(fanout),
                                     std::get<std::int64_t>(length),
                                     std::get<std::int64_t>(cost)});
    }
    auto const library_end = require_words(cursor, {".e"});
    if (is_error(library_end)) {
        return std::get<Error>(library_end);
    }

    auto const objective_header = require_words(cursor, {".objective"});
    if (is_error(objective_header)) {
        return std::get<Error>(objective_header);
    }
    auto const weight_line_result = require_line(cursor, "w_skew <weight>");
    if (is_error(weight_line_result)) {
        return std::get<Error>(weight_line_result);
    }
    auto const& weight_line = std::get<LogicalLine>(weight_line_result);
    if (weight_line.words.size() != 2 || weight_line.words[0] != "w_skew") {
        return Error{line_error(weight_line, "expected 'w_skew <weight>'")};
    }
    auto const weight = require_positive_integer(weight_line.words[1],
                                                 weight_line, "w_skew");
    if (is_error(weight)) {
        return std::get<Error>(weight);
    }
    auto const objective_end = require_words(cursor, {".e"});
    if (is_error(objective_end)) {
        return std::get<Error>(objective_end);
    }

    auto const pin_header_result = require_line(cursor, ".pin <count>");
    if (is_error(pin_header_result)) {
        return std::get<Error>(pin_header_result);
    }
    auto const& pin_header = std::get<LogicalLine>(pin_header_result);
    if (pin_header.words.size() != 2 || pin_header.words[0] != ".pin") {
        return Error{line_error(pin_header, "expected '.pin <count>'")};
    }
    auto const pin_count_result = require_positive_integer(
        pin_header.words[1], pin_header, "pin count");
    if (is_error(pin_count_result)) {
        return std::get<Error>(pin_count_result);
    }
    auto const pin_count = std::get<std::int64_t>(pin_count_result);
    if (pin_count < 2) {
        return Error{line_error(pin_header,
                                "pin count must include SRC and at least one sink")};
    }

    std::vector<Position> pins;
    std::set<std::pair<std::int64_t, std::int64_t>> pin_positions;
    for (std::int64_t index = 0; index < pin_count; ++index) {
        auto const candidate = require_line(cursor, "a pin coordinate");
        if (is_error(candidate)) {
            return std::get<Error>(candidate);
        }
        auto const& line = std::get<LogicalLine>(candidate);
        if (line.words.size() != 2) {
            return Error{line_error(line, "expected '<x> <y>'")};
        }
        auto const x = parse_integer(line.words[0], line, "pin x");
        auto const y = parse_integer(line.words[1], line, "pin y");
        if (is_error(x)) {
            return std::get<Error>(x);
        }
        if (is_error(y)) {
            return std::get<Error>(y);
        }
        auto const position =
            Position{std::get<std::int64_t>(x), std::get<std::int64_t>(y)};
        if (position.x < 0 || position.x > *dimension_x || position.y < 0 ||
            position.y > *dimension_y) {
            return Error{line_error(line, "pin coordinate is outside the chip")};
        }
        if (!pin_positions.emplace(position.x, position.y).second) {
            return Error{line_error(line, "pin coordinates must be distinct")};
        }
        pins.push_back(position);
    }
    auto const pin_end = require_words(cursor, {".e"});
    if (is_error(pin_end)) {
        return std::get<Error>(pin_end);
    }
    if (!cursor.at_end()) {
        auto const extra = cursor.next();
        return Error{line_error(extra->get(), "unexpected content after .pin")};
    }

    auto const source = pins.front();
    std::vector<Position> sinks{pins.begin() + 1, pins.end()};
    return Problem{SourceLimits{*source_fanout, *source_length},
                   Dimensions{*dimension_x, *dimension_y},
                   std::move(library),
                   std::get<std::int64_t>(weight),
                   source,
                   std::move(sinks)};
}


Expected<Problem> read_problem(std::string const& path) {
    auto const lines = read_logical_lines(path);
    if (is_error(lines)) return std::get<Error>(lines);
    return parse_problem(std::get<std::vector<LogicalLine>>(lines));
}
}
