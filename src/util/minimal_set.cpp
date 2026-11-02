#include "util/minimal_set.hpp"

#include <algorithm>
#include <cstddef>
#include <functional>
#include <optional>
#include <utility>
#include <vector>

namespace primo::util {

MinimalSetResult minimal_set_progression(
    const std::vector<std::size_t>& indices,
    const std::function<std::optional<bool>(const std::vector<std::size_t>&)>& predicate) {
  MinimalSetResult result;
  std::vector<std::size_t> necessary;
  std::size_t cursor = 0;
  std::size_t width = 1;
  const auto query = [&](std::size_t skip, bool binary) {
    std::vector<std::size_t> trial = necessary;
    trial.insert(trial.end(), indices.begin() + static_cast<std::ptrdiff_t>(cursor + skip),
                 indices.end());
    ++result.statistics.predicate_calls;
    if (binary) {
      ++result.statistics.binary_queries;
    } else {
      ++result.statistics.block_queries;
    }
    return predicate(trial);
  };
  while (cursor < indices.size()) {
    const std::size_t count = std::min(width, indices.size() - cursor);
    result.statistics.max_block_size = std::max(result.statistics.max_block_size, count);
    auto holds = query(count, false);
    if (!holds.has_value()) {
      result.elements = indices;
      return result;
    }
    if (*holds) {
      cursor += count;
      width = width > indices.size() / 2 ? indices.size() : width * 2;
      continue;
    }
    // The current set is known true. Dropping 'count' items is known false.
    // Locate the first removal boundary that makes the predicate false.
    std::size_t left = 0;
    std::size_t right = count;
    while (left + 1 < right) {
      const std::size_t middle = left + (right - left) / 2;
      holds = query(middle, true);
      if (!holds.has_value()) {
        result.elements = indices;
        return result;
      }
      if (*holds) {
        left = middle;
      } else {
        right = middle;
      }
    }
    necessary.push_back(indices[cursor + right - 1]);
    cursor += right;
    width = 1;
  }
  result.complete = true;
  result.elements = std::move(necessary);
  return result;
}

}  // namespace primo::util
