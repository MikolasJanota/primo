#include "smt/experimental_tactics.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "core/ids.hpp"
#include "core/term.hpp"
#include "core/term_manager.hpp"
#include "smt/strategy.hpp"
#include "smt/tactic.hpp"
#include "util/statistics.hpp"

namespace primo::smt {
namespace {

bool is_boolean_symbol(const core::Term& term, core::SortId bool_sort) {
  return term.sort == bool_sort &&
         (term.kind == core::TermKind::Constant || term.kind == core::TermKind::Application);
}

bool is_theory_atom(const core::Term& term, core::SortId bool_sort) {
  if (term.sort != bool_sort) {
    return false;
  }
  return term.kind == core::TermKind::Equality || term.kind == core::TermKind::Le ||
         term.kind == core::TermKind::Lt;
}

std::vector<core::TermId> select_boolean_atoms(const core::TermManager& terms, const Goal& goal,
                                               std::size_t limit) {
  if (limit == 0) {
    return {};
  }
  std::vector<core::TermId> symbols;
  std::vector<core::TermId> theory_atoms;
  std::vector<core::TermId> pending;
  pending.reserve(goal.assertions().size());
  for (auto assertion = goal.assertions().rbegin(); assertion != goal.assertions().rend();
       ++assertion) {
    pending.push_back(*assertion);
  }

  std::vector<std::uint8_t> visited(terms.size(), 0);
  const core::SortId bool_sort = terms.symbols().bool_sort();
  while (!pending.empty()) {
    const core::TermId term_id = pending.back();
    pending.pop_back();
    if (visited[term_id] != 0) {
      continue;
    }
    visited[term_id] = 1;

    const core::Term& term = terms.get(term_id);
    if (is_boolean_symbol(term, bool_sort)) {
      symbols.push_back(term_id);
      if (symbols.size() == limit) {
        break;
      }
    } else if (is_theory_atom(term, bool_sort)) {
      theory_atoms.push_back(term_id);
    }
    for (auto child = term.children.rbegin(); child != term.children.rend(); ++child) {
      pending.push_back(*child);
    }
  }

  std::vector<core::TermId> selected;
  selected.reserve(std::min(limit, symbols.size() + theory_atoms.size()));
  const auto append_until_limit = [&selected, limit](const std::vector<core::TermId>& candidates) {
    for (const core::TermId candidate : candidates) {
      if (selected.size() == limit) {
        break;
      }
      selected.push_back(candidate);
    }
  };
  append_until_limit(symbols);
  append_until_limit(theory_atoms);
  return selected;
}

AnyOf split_goal(TacticContext& context, const Goal& goal,
                 const ExperimentalSplitBoolsOptions& options) {
  const std::vector<core::TermId> selected =
      select_boolean_atoms(context.terms(), goal, options.max_bools);
  const std::size_t goal_count = std::size_t{1} << selected.size();
  context.statistics().increment_dynamic(":experimental.split-bools.input-goals");
  context.statistics().increment_dynamic(":experimental.split-bools.selected", selected.size());
  context.statistics().increment_dynamic(":experimental.split-bools.output-goals", goal_count);

  if (selected.empty()) {
    return AnyOf::single(goal);
  }

  std::vector<Goal> alternatives;
  alternatives.reserve(goal_count);
  for (std::size_t assignment = 0; assignment < goal_count; ++assignment) {
    std::vector<core::TermId> assertions = goal.assertions();
    assertions.reserve(assertions.size() + selected.size());
    for (std::size_t atom_index = 0; atom_index < selected.size(); ++atom_index) {
      const std::size_t bit_index = selected.size() - atom_index - 1;
      const bool value = (assignment & (std::size_t{1} << bit_index)) != 0;
      assertions.push_back(value ? selected[atom_index]
                                 : context.terms().mk_not(selected[atom_index]));
    }
    alternatives.push_back(goal.with_assertions(std::move(assertions)));
  }
  return AnyOf(std::move(alternatives));
}

}  // namespace

std::vector<StrategyParameter> ExperimentalSplitBoolsOptions::strategy_parameters() const {
  return {{"k", std::to_string(max_bools)}};
}

Tactic make_experimental_split_bools(ExperimentalSplitBoolsOptions options) {
  if (options.max_bools > kMaxExperimentalSplitBools) {
    throw std::invalid_argument("experimental split-bools k exceeds the materialization limit");
  }
  return make_goal_tactic("experimental-split-bools",
                          [options](TacticContext& context, const Goal& goal) {
                            return split_goal(context, goal, options);
                          });
}

}  // namespace primo::smt
