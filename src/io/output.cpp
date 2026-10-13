#include "io/output.hpp"
#include <fstream>
#include <algorithm>
#include <functional>
#include <limits>
#include <numeric>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <tuple>


namespace cbi {
std::string serialize_tree(Problem const& p, Tree const& tree) {
    std::vector<std::string> names(tree.size());
    std::size_t buffers = 0, sinks = 0;
    names[0] = "SRC";
    for (std::size_t i = 1; i < tree.size(); ++i)
        names[i] = tree[i].type == -2 ? "S" + std::to_string(++sinks) : "B" + std::to_string(++buffers);
    std::ostringstream output;
    output << ".buffer " << buffers << '\n';
    for (std::size_t i = 1; i < tree.size(); ++i) {
        if (tree[i].type < 0) continue;
        output << names[i] << ' ' << p.buffer_library[static_cast<std::size_t>(tree[i].type)].name
               << ' ' << tree[i].position.x << ' ' << tree[i].position.y << '\n';
    }
    std::vector<std::size_t> queue{0};
    std::vector<std::size_t> depth(tree.size());
    std::size_t max_depth = 0;
    for (std::size_t k = 0; k < queue.size(); ++k) {
        auto const u = queue[k];
        for (auto v : tree[u].children) {
            depth[v] = depth[u] + 1;
            max_depth = std::max(max_depth, depth[v]);
            queue.push_back(v);
        }
    }
    output << ".e\n\n.level " << max_depth << '\n';
    std::size_t current_level = 0;
    for (auto u : queue) {
        if (tree[u].children.empty()) continue;
        auto const level = depth[u] + 1;
        if (level != current_level) {
            if (current_level != 0) output << '\n';
            output << level;
            current_level = level;
        }
        output << ' ' << names[u] << ":{";
        for (std::size_t k = 0; k < tree[u].children.size(); ++k) {
            if (k != 0) output << ' ';
            output << names[tree[u].children[k]];
        }
        output << '}';
    }
    output << "\n.e\n";
    return output.str();
}

Expected<WriteSuccess> write_output(std::string const& output_path,
                                    std::string const& contents) {
    std::ofstream output{output_path};
    if (!output) {
        return Error{"cannot open output file '" + output_path + "'"};
    }
    output << contents;
    if (!output) {
        return Error{"failed while writing output file '" + output_path + "'"};
    }
    return WriteSuccess{};
}


}
