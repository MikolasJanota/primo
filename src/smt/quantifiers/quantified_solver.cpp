#include "smt/quantifiers/quantified_solver.hpp"

#include <memory>
#include <unordered_map>
#include <utility>

#include "core/binders.hpp"
#include "core/term.hpp"
#include "core/term_manager.hpp"
#include "smt/component_solver.hpp"
#include "smt/preprocessing.hpp"
#include "smt/quantifiers/pool_export.hpp"
#include "smt/solver_engines.hpp"
#include "theory/quantifiers/quantifier_component.hpp"
#include "util/statistics.hpp"

namespace primo::smt {

namespace {

// EUF expects every compound Boolean argument of an application to be named by
// a constant whose definition SAT owns (docs/euf-boolean-arguments.md). The
// frontend names ground arguments while parsing but leaves open ones inside
// quantifier bodies; their instances are named here, when they become lemmas.
// Quantified subformulas stay opaque.
std::vector<core::TermId> name_boolean_arguments(core::TermManager& terms,
                                                 const std::vector<core::TermId>& roots,
                                                 const FreshConstantFactory& fresh_constant) {
  const core::SortId boolean = terms.symbols().bool_sort();
  std::unordered_map<core::TermId, core::TermId> rewritten;
  std::unordered_map<core::TermId, core::TermId> names;
  std::vector<core::TermId> definitions;
  std::vector<core::TermId> result;
  struct Visit {
    core::TermId term = core::kInvalidTermId;
    bool expanded = false;
  };
  for (const core::TermId root : roots) {
    std::vector<Visit> pending{{root, false}};
    while (!pending.empty()) {
      const Visit visit = pending.back();
      pending.pop_back();
      if (rewritten.contains(visit.term)) {
        continue;
      }
      const core::Term& node = terms.get(visit.term);
      const core::TermKind kind = node.kind;
      if (node.children.empty() || kind == core::TermKind::Forall ||
          kind == core::TermKind::Exists) {
        rewritten.emplace(visit.term, visit.term);
        continue;
      }
      const std::vector<core::TermId> children = node.children;
      if (!visit.expanded) {
        pending.push_back({visit.term, true});
        for (auto child = children.rbegin(); child != children.rend(); ++child) {
          if (!rewritten.contains(*child)) {
            pending.push_back({*child, false});
          }
        }
        continue;
      }
      std::vector<core::TermId> arguments;
      arguments.reserve(children.size());
      bool changed = false;
      for (const core::TermId child : children) {
        core::TermId argument = rewritten.at(child);
        const core::Term& value = terms.get(argument);
        if (kind == core::TermKind::Application && value.sort == boolean &&
            value.kind != core::TermKind::BoolValue && value.kind != core::TermKind::Constant) {
          const auto found = names.find(argument);
          if (found != names.end()) {
            argument = found->second;
          } else {
            const core::TermId name = fresh_constant(boolean, "bool_arg");
            definitions.push_back(
                terms.mk_and({terms.mk_implies(name, argument), terms.mk_implies(argument, name)}));
            names.emplace(argument, name);
            argument = name;
          }
        }
        changed = changed || argument != child;
        arguments.push_back(argument);
      }
      rewritten.emplace(
          visit.term,
          changed ? core::rebuild_operator(terms, visit.term, std::move(arguments)) : visit.term);
    }
    result.push_back(rewritten.at(root));
  }
  result.insert(result.end(), definitions.begin(), definitions.end());
  return result;
}

}  // namespace

CheckSatOutcome solve_quantified_uf(
    core::TermManager& terms, const std::vector<core::TermId>& assertions,
    const CheckSatOptions& requested, util::Statistics& statistics,
    const std::function<core::TermId(core::SortId, std::string_view)>& fresh_constant) {
  statistics.increment(util::StatKey::SolverQuantifiedUf);
  CheckSatOptions options = requested;
  // The pure-theory filter skips theory assignments against an atom's only
  // polarity, which presumes the clause set is final. Instance lemmas keep
  // adding occurrences, so an atom pure when registered can stop being pure
  // and its skipped assignments matter: with the filter, instances reusing
  // ground atoms in the other orientation lost refutations (sledgehammer
  // FFT/uf.860560). Incremental sessions turn it off for the same reason.
  options.pure_theory_filter = PureTheoryFilterMode::Off;
  if (options.produce_model) {
    return {CheckSatResult::Unsupported, "models for quantified formulas are not supported",
            statistics, "", InconclusiveReason::Unsupported};
  }
#if !PRIMO_HAVE_IPASIR
  return {CheckSatResult::Unsupported, "quantified formulas require an IPASIR backend", statistics,
          "", InconclusiveReason::Unsupported};
#endif

  const FreshConstantFactory factory = fresh_constant;
  const std::vector<core::TermId> ground =
      lower_extended_terms(terms, assertions, factory).assertions;

  theory::QuantifierInstantiationOptions instantiation;
  instantiation.instances_per_round = options.quantifier_instances_per_round;
  instantiation.stall_tuples = options.quantifier_stall_tuples;
  instantiation.max_instances = options.quantifier_max_instances;
  instantiation.simplify_instances = options.quantifier_instance_simplification;
  instantiation.tuple_order = options.quantifier_tuple_order;
  instantiation.deepening_increment = options.quantifier_deepening_increment;
  instantiation.random_seed = options.quantifier_random_seed;
  theory::QuantifierExport exported;
  if (!options.quantifier_export_pools.empty()) instantiation.export_sink = &exported;

  CombinationRecipe recipe;
  recipe.online = true;
  recipe.model_guided = false;
  recipe.online_order = {0, 1};
  recipe.timing_prefix = ":time.quantified-uf.";
  recipe.prepare_boolean = [&](BooleanClausifier& clausifier) {
    register_euf_boolean_arguments(clausifier, statistics);
  };
  recipe.prepare_lemmas = [&](std::vector<core::TermId> lemmas) {
    return lower_extended_terms(terms, name_boolean_arguments(terms, lemmas, factory), factory)
        .assertions;
  };
  recipe.factories = {[&](bool) { return make_uf_euf_component(terms, options, statistics, true); },
                      [&](bool) -> std::unique_ptr<theory::TheoryComponent> {
                        return std::make_unique<theory::QuantifierComponent>(
                            terms, ground, factory, instantiation, statistics);
                      }};
  CheckSatOutcome outcome = solve_component_recipe(terms, ground, {}, options, statistics, recipe);
  if (instantiation.export_sink != nullptr) {
    write_pool_export(terms, ground, exported, options.quantifier_export_pools, statistics);
    outcome.statistics = statistics;
  }
  return outcome;
}

}  // namespace primo::smt
