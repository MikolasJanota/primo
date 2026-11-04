#include "smt/boolean_solver.hpp"

#include <optional>
#include <utility>
#include <vector>

#include "core/ids.hpp"
#include "core/term_manager.hpp"
#include "sat/cadical_backend.hpp"
#include "smt/check_sat.hpp"
#include "smt/clausifier.hpp"
#include "smt/cnf_export.hpp"
#include "smt/model_reconstruction.hpp"
#include "smt/solver_runtime.hpp"
#include "smt/solver_support.hpp"
#include "smt/typed_model_builder.hpp"
#include "util/statistics.hpp"

namespace primo::smt {

CheckSatOutcome solve_boolean_conjunction(core::TermManager& terms,
                                          const std::vector<core::TermId>& assertions,
                                          const CheckSatOptions& options,
                                          util::Statistics& statistics,
                                          const ModelConversionChain& conversions,
                                          CnfConflictCollector* collector) {
  statistics.increment(util::StatKey::SolverBoolean);
  sat::CadicalBackend backend(options.cadical_options);
  SolveTerminator terminator(options.cancelled, statistics, options.max_theory_conflicts);
  const ScopedTerminatorConnection terminator_connection(backend, terminator);
  BooleanClausifier::ClauseObserver observer;
  if (collector != nullptr) {
    observer = [collector](const sat::Clause& clause) { collector->record_input_clause(clause); };
  }
  BooleanClausifier clausifier(backend, terms, options.top_level_or_tseitin, std::move(observer));
  for (const core::TermId assertion : assertions) {
    clausifier.assert_formula(assertion);
  }

  if (collector != nullptr) collector->bind(clausifier);

  statistics.set(util::StatKey::TheoryAtoms, clausifier.theory_atoms().size());
  publish_theory_atom_purity_stats(terms, clausifier.theory_atoms(), statistics);
  publish_sat_stats(backend.statistics(), statistics);
  statistics.increment(util::StatKey::SatSolveCalls);
  if (auto stop = terminator.stop_outcome()) return *stop;
  const CheckSatResult result = map_sat_result(backend.solve());
  publish_sat_stats(backend.statistics(), statistics);
  if (auto stop = terminator.stop_outcome()) return *stop;
  if (result != CheckSatResult::Sat || !options.produce_model) {
    return CheckSatOutcome{result, "", statistics, ""};
  }

  std::vector<TheoryModelAssignment> model_assignments;
  model_assignments.reserve(model_assignments.size() + clausifier.theory_atoms().size());
  for (const BooleanClausifier::TheoryAtom& atom : clausifier.theory_atoms()) {
    const int variable = atom.lit.value > 0 ? atom.lit.value : -atom.lit.value;
    const std::optional<bool> assignment = backend.value(variable);
    if (!assignment.has_value()) {
      return CheckSatOutcome{CheckSatResult::Unknown, "SAT backend did not assign theory atom",
                             statistics, ""};
    }
    const bool value = atom.lit.value > 0 ? assignment.value() : !assignment.value();
    model_assignments.push_back(TheoryModelAssignment{atom.term, value, false});
  }
  CheckSatOutcome outcome{result, "", statistics, ""};
  outcome.typed_model = build_typed_model(terms, assertions, model_assignments, {}, conversions,
                                          nullptr, &statistics);
  return outcome;
}

}  // namespace primo::smt
