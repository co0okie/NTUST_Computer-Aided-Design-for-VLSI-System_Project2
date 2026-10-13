#pragma once
#include "core/tree.hpp"
#include <memory>
namespace cbi {
class BuilderContext;
class Builder final {
    std::unique_ptr<BuilderContext> impl_;

  public:
    Builder(Problem const &, bool parity, int relay_policy = -1, int branch_policy = -1,
            bool optimize_allocation = false);
    ~Builder();
    Builder(Builder &&) noexcept;
    Builder &operator=(Builder &&) noexcept;
    Builder(Builder const &) = delete;
    Builder &operator=(Builder const &) = delete;
    Tree build();
    Tree build_star();
    Tree build_hub();
    Tree build_multiway(std::size_t source_groups, std::size_t &work);
    Tree build_layered(std::size_t groups, std::size_t &work);
};
} // namespace cbi
