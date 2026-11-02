#pragma once

#include <cstddef>
#include <functional>
#include <optional>
#include <vector>

namespace primo::util {

struct ProgressionStatistics {
  std::size_t predicate_calls = 0;
  std::size_t block_queries = 0;
  std::size_t binary_queries = 0;
  std::size_t max_block_size = 0;
};

struct MinimalSetResult {
  bool complete = false;
  std::vector<std::size_t> elements;
  ProgressionStatistics statistics;
};

// CAV 2013, Marques-Silva/Janota/Belov, Algorithms 3-4; see
// docs/leads/theory-lemma-reduction.md. The predicate is monotone, is true on
// the supplied distinct indices, and may return unknown. It must not mutate
// indices. An incomplete result returns the original known-true set.
MinimalSetResult minimal_set_progression(
    const std::vector<std::size_t>& indices,
    const std::function<std::optional<bool>(const std::vector<std::size_t>&)>& predicate);

}  // namespace primo::util
