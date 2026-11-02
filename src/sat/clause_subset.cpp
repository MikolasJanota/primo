#include "sat/clause_subset.hpp"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <optional>
#include <stdexcept>
#include <vector>

#include "sat/cadical_backend.hpp"
#include "sat/clause.hpp"
#include "sat/cnf.hpp"
#include "sat/ipasir_up.hpp"
#include "sat/lit.hpp"
#include "util/minimal_set.hpp"

namespace primo::sat {

ClauseSubsetResult select_unsat_subset(const Cnf& hard, const Cnf& candidates,
                                       ClauseSubsetMode mode, Terminator* terminator) {
  if (mode != ClauseSubsetMode::Core && mode != ClauseSubsetMode::Mus) {
    throw std::invalid_argument("invalid clause-subset reduction mode");
  }
  ClauseSubsetResult result;
  int max_variable = 0;
  const auto scan = [&](const Cnf& cnf) {
    for (const auto& clause : cnf) {
      for (const auto literal : clause) {
        if (literal.value == 0 || literal.value == std::numeric_limits<int>::min()) {
          throw std::invalid_argument("invalid literal in clause-subset input");
        }
        const int variable = literal.value < 0 ? -literal.value : literal.value;
        max_variable = std::max(max_variable, variable);
      }
    }
  };
  scan(hard);
  scan(candidates);
  if (candidates.size() >
      static_cast<std::size_t>(std::numeric_limits<int>::max() - max_variable)) {
    throw std::invalid_argument("too many selector variables for clause-subset input");
  }
  // Original selector-based oracle; initial core refinement follows CAV 2013,
  // Section 4.3. The progression stage uses Algorithms 3-4. The fixed hard CNF
  // makes this a relative MUS problem; see docs/leads/theory-lemma-reduction.md.
  CadicalBackend backend({}, true);
  if (terminator != nullptr) backend.connect_terminator(terminator);
  for (int variable = 0; variable < max_variable; ++variable) {
    static_cast<void>(backend.new_var());
  }
  for (const auto& clause : hard) backend.add_clause(clause);
  std::vector<Lit> selectors;
  selectors.reserve(candidates.size());
  for (const auto& clause : candidates) {
    const Lit selector{backend.new_var()};
    selectors.push_back(selector);
    Clause guarded{Lit{-selector.value}};
    guarded.insert(guarded.end(), clause.begin(), clause.end());
    backend.add_clause(guarded);
  }
  const auto query = [&](const std::vector<std::size_t>& selected) {
    if (terminator != nullptr && terminator->terminate()) return SatResult::Unknown;
    for (const auto index : selected) backend.assume(selectors[index]);
    ++result.sat_calls;
    return backend.solve();
  };
  std::vector<std::size_t> all;
  all.reserve(candidates.size());
  for (std::size_t index = 0; index < candidates.size(); ++index) all.push_back(index);
  result.result = query(all);
  if (result.result != SatResult::Unsat) return result;
  for (const auto index : all) {
    if (backend.failed(selectors[index])) result.selected.push_back(index);
  }
  result.core_removed = candidates.size() - result.selected.size();
  if (mode == ClauseSubsetMode::Core) return result;

  SatResult last_result = SatResult::Unsat;
  const auto minimal = util::minimal_set_progression(
      result.selected, [&](const std::vector<std::size_t>& selected) -> std::optional<bool> {
        last_result = query(selected);
        if (last_result == SatResult::Unsat) return true;
        if (last_result == SatResult::Sat) return false;
        return std::nullopt;
      });
  result.progression = minimal.statistics;
  if (minimal.complete) {
    result.selected = minimal.elements;
  } else {
    result.result = last_result;
  }
  return result;
}

}  // namespace primo::sat
