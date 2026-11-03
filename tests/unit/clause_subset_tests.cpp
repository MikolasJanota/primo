#ifdef NDEBUG
#undef NDEBUG
#endif

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <numeric>
#include <optional>
#include <stdexcept>
#include <vector>

#include "sat/cadical_backend.hpp"
#include "sat/clause_subset.hpp"
#include "sat/cnf.hpp"
#include "sat/ipasir_up.hpp"
#include "sat/lit.hpp"
#include "util/minimal_set.hpp"

using primo::sat::ClauseSubsetMode;
using primo::sat::Cnf;
using primo::sat::Lit;
using primo::sat::SatResult;

namespace {

// Replay without selectors in a new backend, independent of the subset oracle.
SatResult replay(const Cnf& hard, const Cnf& candidates, const std::vector<std::size_t>& selected) {
  primo::sat::CadicalBackend backend;
  // The backend requires all user variables to be declared before adding clauses.
  int maximum = 0;
  const auto scan = [&](const auto& clause) {
    for (const auto literal : clause) {
      maximum = std::max(maximum, literal.value < 0 ? -literal.value : literal.value);
    }
  };
  for (const auto& clause : hard) scan(clause);
  for (const auto index : selected) scan(candidates.at(index));
  for (int variable = 0; variable < maximum; ++variable) {
    static_cast<void>(backend.new_var());
  }
  for (const auto& clause : hard) backend.add_clause(clause);
  for (const auto index : selected) backend.add_clause(candidates.at(index));
  return backend.solve();
}

void verify(const Cnf& hard, const Cnf& candidates, ClauseSubsetMode mode) {
  const auto result = primo::sat::select_unsat_subset(hard, candidates, mode);
  assert(result.result == SatResult::Unsat);
  assert(std::is_sorted(result.selected.begin(), result.selected.end()));
  assert(replay(hard, candidates, result.selected) == SatResult::Unsat);
  if (mode == ClauseSubsetMode::Mus) {
    for (std::size_t position = 0; position < result.selected.size(); ++position) {
      auto trial = result.selected;
      trial.erase(trial.begin() + static_cast<std::ptrdiff_t>(position));
      assert(replay(hard, candidates, trial) == SatResult::Sat);
    }
  } else {
    assert(result.sat_calls == 1);
  }
}

void subset_cases() {
  for (const auto mode : {ClauseSubsetMode::Core, ClauseSubsetMode::Mus}) {
    // Duplicate units, tautology and an irrelevant clause are selectable.
    verify({{Lit{1}, Lit{2}}}, {{Lit{-1}}, {Lit{-2}}, {Lit{-1}}, {Lit{3}}, {Lit{2}, Lit{-2}}},
           mode);
    verify({{Lit{1}}, {Lit{-1}}}, {{Lit{2}}, {}}, mode);
    verify({}, {{Lit{1}}, {Lit{-1}}}, mode);
    verify({}, {{}}, mode);
    verify({{}}, {}, mode);
    const auto hard_unsat = primo::sat::select_unsat_subset({{}}, {{Lit{1}}}, mode);
    assert(hard_unsat.result == SatResult::Unsat && hard_unsat.selected.empty());
    const auto satisfiable = primo::sat::select_unsat_subset({}, {{Lit{1}}}, mode);
    assert(satisfiable.result == SatResult::Sat);
  }
  // Exhaust every subset of a small clause pool. Each UNSAT result is checked
  // again after removing every reported MUS member.
  const Cnf pool{{Lit{1}}, {Lit{-1}}, {Lit{2}}, {Lit{-2}}, {Lit{1}, Lit{2}}};
  for (unsigned mask = 0; mask < (1U << pool.size()); ++mask) {
    Cnf candidates;
    for (std::size_t index = 0; index < pool.size(); ++index) {
      if ((mask & (1U << index)) != 0) candidates.push_back(pool[index]);
    }
    std::vector<std::size_t> all(candidates.size());
    std::iota(all.begin(), all.end(), 0);
    if (replay({}, candidates, all) == SatResult::Unsat) {
      verify({}, candidates, ClauseSubsetMode::Mus);
    }
  }
}

void progression_cases() {
  std::vector<std::size_t> indices(1024);
  std::iota(indices.begin(), indices.end(), 0);
  const auto result = primo::util::minimal_set_progression(
      indices, [](const std::vector<std::size_t>& trial) -> std::optional<bool> {
        return std::find(trial.begin(), trial.end(), 1000) != trial.end();
      });
  assert(result.complete && result.elements == std::vector<std::size_t>{1000});
  assert(result.statistics.max_block_size > 1);
  assert(result.statistics.binary_queries > 0);
  assert(result.statistics.predicate_calls < 40);
  std::size_t calls = 0;
  const auto interrupted = primo::util::minimal_set_progression(
      indices, [&](const std::vector<std::size_t>&) -> std::optional<bool> {
        if (++calls == 3) return std::nullopt;
        return true;
      });
  assert(!interrupted.complete && interrupted.elements == indices);
  const auto redundant = primo::util::minimal_set_progression(
      indices, [](const auto&) -> std::optional<bool> { return true; });
  assert(redundant.complete && redundant.elements.empty());
  const std::vector<std::size_t> small{0, 1, 2};
  const auto necessary = primo::util::minimal_set_progression(
      small, [](const auto& trial) -> std::optional<bool> { return trial.size() == 3; });
  assert(necessary.complete && necessary.elements == small);
}

class Cancelled final : public primo::sat::Terminator {
 public:
  bool terminate() override { return true; }
};

void interruption_and_invalid_input() {
  Cancelled cancelled;
  const auto result =
      primo::sat::select_unsat_subset({}, {{Lit{1}}, {Lit{-1}}}, ClauseSubsetMode::Mus, &cancelled);
  assert(result.result == SatResult::Unknown && result.sat_calls == 0);
  bool rejected = false;
  try {
    static_cast<void>(primo::sat::select_unsat_subset({}, {{Lit{0}}}, ClauseSubsetMode::Core));
  } catch (const std::invalid_argument&) {
    rejected = true;
  }
  assert(rejected);
}

}  // namespace

int main() {
  progression_cases();
  subset_cases();
  interruption_and_invalid_input();
}
