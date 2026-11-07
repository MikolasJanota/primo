#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "core/term_manager.hpp"
#include "smt/check_sat.hpp"
#include "smt/clausifier.hpp"
#include "smt/model_reconstruction.hpp"
#include "theory/theory_component.hpp"
#include "util/statistics.hpp"

namespace primo::smt {

struct CombinationRecipe {
  // Factory order is the model reconciliation priority; online_order is an
  // independent permutation. The scheduler has no theory-name branches.
  std::vector<std::function<std::unique_ptr<theory::TheoryComponent>(bool)>> factories;
  std::vector<std::size_t> online_order;
  bool online = false;
  // Preserve each recipe's SAT clause-retention policy across dispatcher rebuilds.
  bool forgettable_clauses = true;
  bool model_guided = true;
  // Diagnostic conservative naming: install bridges for fresh proved pairs,
  // but omit the optional proof clauses. Producers must request only fresh pairs.
  // Original design: docs/implementation/uflia-equality-ablation.md.
  bool learn_exported_equalities = true;
  std::size_t decision_budget = 0;
  std::string timing_prefix;
  // Idempotent registry completion before initial ownership planning and at
  // every atom installation, including generated interface/lemma atoms.
  // May append atoms; callers must not borrow the registry across this hook.
  // See docs/euf-boolean-arguments.md for the generated-term contract.
  std::function<void(BooleanClausifier&)> prepare_boolean;
  // Prepares the lemmas an online component queued with LemmasPending -- for
  // example by lowering extended terms -- and returns the formulas to
  // clausify. Without it such a stop is Unsupported. `lemma_batch` bounds how
  // many queued lemmas are drained at each growth boundary.
  std::function<std::vector<core::TermId>(std::vector<core::TermId>)> prepare_lemmas;
  std::size_t lemma_batch = 64;
};

CheckSatOutcome solve_component_recipe(core::TermManager& terms,
                                       std::span<const core::TermId> assertions,
                                       const ModelConversionChain& conversions,
                                       const CheckSatOptions& options, util::Statistics& statistics,
                                       const CombinationRecipe& recipe);

}  // namespace primo::smt
