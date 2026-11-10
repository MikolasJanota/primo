#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <limits>
#include <locale>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "core/ids.hpp"
#include "core/model.hpp"
#include "core/symbol_table.hpp"
#include "core/term_manager.hpp"
#include "smt/check_sat.hpp"
#include "smt/experimental_tactics.hpp"
#include "smt/frontend.hpp"
#include "smt/inconclusive_reason.hpp"
#include "smt/lia_settings.hpp"
#include "smt/lia_settings_policy.hpp"
#include "smt/model_reconstruction.hpp"
#include "smt/preprocessing.hpp"
#include "smt/preprocessing_strategy.hpp"
#include "smt/preprocessing_tactics.hpp"
#include "smt/session_portfolio.hpp"
#include "smt/solver_answer.hpp"
#include "smt/solver_error.hpp"
#include "smt/solver_leaves.hpp"
#include "smt/solver_strategy.hpp"
#include "smt/solver_support.hpp"
#include "smt/solver_tactic.hpp"
#include "smt/strategy.hpp"
#include "smt/tactic.hpp"
#include "util/statistics.hpp"

namespace {

void check(bool condition) {
  if (!condition) {
    std::abort();
  }
}

struct Fixture {
  primo::core::SymbolTable symbols;
  primo::core::SymbolId p = symbols.declare_constant("p", symbols.bool_sort());
  primo::core::TermManager terms{symbols};
  primo::util::Statistics statistics;
  primo::smt::TacticContext context{terms, statistics};

  primo::core::TermId p_term() { return terms.mk_constant(p); }
};

void incremental_context_rejects_unclassified_tactics() {
  Fixture fixture;
  fixture.context.require_incremental_safety();
  const auto input = primo::smt::AnyOf::single(primo::smt::Goal({fixture.p_term()}));
  bool rejected = false;
  try {
    static_cast<void>(primo::smt::make_propagate_values()(fixture.context, input));
  } catch (const primo::smt::TacticError&) {
    rejected = true;
  }
  check(rejected);
  const auto output = primo::smt::make_normalize_boolean({})(fixture.context, input);
  check(output == input);
}

std::vector<primo::smt::TheoryModelAssignment> only_boolean_assignments(
    const primo::smt::ModelConversionChain& conversions) {
  check(conversions.size() == 1);
  std::vector<primo::smt::TheoryModelAssignment> assignments;
  conversions.replay_reverse([&](const primo::smt::ModelConversionStep& step) {
    assignments = std::get<primo::smt::BooleanTheoryAssignmentConversion>(step).assignments;
  });
  return assignments;
}

struct IdentityStrategyParameters {
  std::string alpha;
  std::string zeta;

  std::vector<primo::smt::StrategyParameter> strategy_parameters() const {
    return {{"zeta", zeta}, {"alpha", alpha}};
  }
};

class RecordingSessionExecutor final : public primo::smt::SessionExecutor {
 public:
  std::vector<primo::smt::CheckSatOutcome> outcomes;
  std::vector<primo::smt::CheckSatOptions> options;
  bool throw_on_execute = false;

  primo::smt::CheckSatOutcome execute(std::string_view,
                                      primo::smt::CheckSatOptions next_options) override {
    options.push_back(std::move(next_options));
    if (throw_on_execute) {
      throw std::runtime_error("executor failure");
    }
    check(options.size() <= outcomes.size());
    return outcomes[options.size() - 1];
  }
};

void goal_truth_is_derived_from_assertions() {
  Fixture fixture;
  static_assert(std::is_same_v<decltype(std::declval<const primo::smt::Goal&>().assertions()),
                               const std::vector<primo::core::TermId>&>);
  check(primo::smt::truth(primo::smt::Goal{}, fixture.context) == primo::smt::GoalTruth::True);
  check(primo::smt::truth(primo::smt::Goal{{fixture.terms.mk_bool(false)}}, fixture.context) ==
        primo::smt::GoalTruth::False);
  check(primo::smt::truth(primo::smt::Goal{{fixture.p_term(), fixture.terms.mk_bool(false)}},
                          fixture.context) == primo::smt::GoalTruth::False);
  check(primo::smt::truth(primo::smt::Goal{{fixture.p_term()}}, fixture.context) ==
        primo::smt::GoalTruth::Inconclusive);

  const primo::smt::Goal original{{fixture.p_term()}};
  const primo::smt::Goal diagnosed =
      original.with_diagnostic(primo::smt::InconclusiveReason::ResourceLimit, "budget");
  check(original.diagnostic().empty());
  check(diagnosed.reason() == primo::smt::InconclusiveReason::ResourceLimit);
  check(diagnosed.assertions() == original.assertions());
}

void reconstruction_records_are_goal_local_immutable_values() {
  Fixture fixture;
  const primo::core::TermId p = fixture.p_term();
  primo::smt::ModelReconstruction reconstruction;
  reconstruction.conversions =
      reconstruction.conversions.appended(primo::smt::BooleanTheoryAssignmentConversion{
          {{.term = p, .value = true, .filtered = false}}});
  reconstruction.forced_equalities.emplace_back(p, p);

  const primo::smt::Goal original{{p}};
  const primo::smt::Goal transformed = original.with_reconstruction(reconstruction);
  reconstruction.conversions = {};

  check(original.reconstruction().conversions.empty());
  const auto assignments = only_boolean_assignments(transformed.reconstruction().conversions);
  check(assignments.size() == 1);
  check(assignments.front().term == p);
  check(transformed.reconstruction().forced_equalities ==
        std::vector<std::pair<primo::core::TermId, primo::core::TermId>>{{p, p}});
  check(transformed.with_assertions({}).reconstruction() == transformed.reconstruction());
}

void goal_updates_share_untouched_large_payloads() {
  Fixture fixture;
  const primo::core::TermId p = fixture.p_term();
  primo::smt::ModelReconstruction reconstruction;
  constexpr std::size_t kLargePayloadSize = 4096;
  primo::smt::BooleanTheoryAssignmentConversion assignments;
  assignments.assignments.reserve(kLargePayloadSize);
  for (std::size_t index = 0; index < kLargePayloadSize; ++index) {
    assignments.assignments.push_back(
        primo::smt::TheoryModelAssignment{.term = p, .value = index % 2 == 0, .filtered = false});
  }
  reconstruction.conversions = reconstruction.conversions.appended(std::move(assignments));
  auto model_symbols = primo::core::SymbolTable{};
  std::vector<primo::core::ModelInterpretation> interpretations;
  for (std::size_t index = 0; index < kLargePayloadSize; ++index) {
    const auto symbol =
        model_symbols.declare_constant("m" + std::to_string(index), model_symbols.bool_sort());
    interpretations.push_back({symbol, {model_symbols.bool_sort(), false}, {}});
  }
  const auto model =
      std::make_shared<const primo::core::Model>(model_symbols, std::move(interpretations));
  const primo::smt::Goal original =
      primo::smt::Goal{{p}}.with_reconstruction(reconstruction).with_typed_model(model);

  const primo::smt::Goal assertions_changed = original.with_assertions({p, p});
  check(&assertions_changed.reconstruction() == &original.reconstruction());
  check(assertions_changed.typed_model() == original.typed_model());

  const primo::smt::Goal diagnostic_changed = original.with_diagnostic("diagnostic");
  check(&diagnostic_changed.assertions() == &original.assertions());
  check(&diagnostic_changed.reconstruction() == &original.reconstruction());
  check(diagnostic_changed.typed_model() == original.typed_model());

  reconstruction.conversions =
      reconstruction.conversions.appended(primo::smt::BooleanTheoryAssignmentConversion{
          {{.term = p, .value = true, .filtered = false}}});
  const primo::smt::Goal reconstruction_changed =
      original.with_reconstruction(std::move(reconstruction));
  check(&reconstruction_changed.assertions() == &original.assertions());
  check(reconstruction_changed.typed_model() == original.typed_model());
  check(reconstruction_changed.reconstruction().conversions.size() == 2);
  check(original.reconstruction().conversions.size() == 1);
}

void conversion_chain_replays_typed_steps_in_reverse_execution_order() {
  primo::smt::ModelConversionChain conversions;
  conversions = conversions.appended(primo::smt::BooleanTheoryAssignmentConversion{});
  conversions = conversions.appended(primo::smt::DirectedEqualitySubstitutionConversion{});
  conversions = conversions.appended(primo::smt::MonotoneEliminationConversion{});

  int value = 0;
  conversions.replay_reverse([&value](const primo::smt::ModelConversionStep& step) {
    std::visit(
        [&value](const auto& conversion) {
          using Conversion = std::decay_t<decltype(conversion)>;
          if constexpr (std::is_same_v<Conversion, primo::smt::MonotoneEliminationConversion>) {
            value = 4;
          } else if constexpr (std::is_same_v<Conversion,
                                              primo::smt::DirectedEqualitySubstitutionConversion>) {
            value += 3;
          } else if constexpr (std::is_same_v<Conversion,
                                              primo::smt::BooleanTheoryAssignmentConversion>) {
            value *= 2;
          }
        },
        step);
  });
  check(value == 14);
}

void anyof_has_disjunctive_semantics() {
  Fixture fixture;
  const primo::smt::Goal unresolved{{fixture.p_term()}};
  const primo::smt::Goal falsified{{fixture.terms.mk_bool(false)}};
  const primo::smt::Goal satisfied;

  check(primo::smt::truth(primo::smt::AnyOf({falsified, unresolved}), fixture.context) ==
        primo::smt::GoalTruth::Inconclusive);
  check(primo::smt::truth(primo::smt::AnyOf({falsified, satisfied}), fixture.context) ==
        primo::smt::GoalTruth::True);
  check(primo::smt::truth(primo::smt::AnyOf({falsified, falsified}), fixture.context) ==
        primo::smt::GoalTruth::False);
}

void empty_anyof_is_rejected() {
  bool threw = false;
  try {
    static_cast<void>(primo::smt::AnyOf({}));
  } catch (const std::invalid_argument&) {
    threw = true;
  }
  check(threw);
}

void non_boolean_goal_assertion_is_a_contract_error() {
  Fixture fixture;
  bool threw = false;
  try {
    static_cast<void>(
        primo::smt::truth(primo::smt::Goal{{fixture.terms.mk_rational(0)}}, fixture.context));
  } catch (const primo::smt::TacticError&) {
    threw = true;
  }
  check(threw);
}

void sequence_and_repeat_stop_at_a_fixed_point() {
  Fixture fixture;
  const primo::core::TermId p = fixture.p_term();
  std::size_t applications = 0;
  const primo::smt::Tactic remove_one = primo::smt::make_goal_tactic(
      "remove-one", [&applications](primo::smt::TacticContext&, const primo::smt::Goal& goal) {
        ++applications;
        std::vector<primo::core::TermId> assertions = goal.assertions();
        assertions.pop_back();
        return primo::smt::AnyOf::single(goal.with_assertions(std::move(assertions)));
      });
  const primo::smt::Tactic repeated = primo::smt::repeat("repeat-remove", remove_one, 8);
  const primo::smt::AnyOf result =
      repeated(fixture.context, primo::smt::AnyOf::single(primo::smt::Goal{{p, p, p}}));
  check(primo::smt::truth(result, fixture.context) == primo::smt::GoalTruth::True);
  check(applications == 3);
  check(fixture.statistics.get(":tactic.remove-one.invocations") == 3);
}

void repeat_preserves_equivalent_output_and_observes_terminal_changes() {
  Fixture fixture;
  const primo::core::TermId p = fixture.p_term();
  const primo::smt::Tactic diagnose = primo::smt::make_goal_tactic(
      "diagnose", [](primo::smt::TacticContext&, const primo::smt::Goal& goal) {
        return primo::smt::AnyOf::single(goal.with_diagnostic("no-progress"));
      });
  const primo::smt::Tactic assertion_fixed_point =
      primo::smt::repeat("assertion-fixed-point", diagnose, 4, {},
                         [](const primo::smt::TacticContext&, const primo::smt::AnyOf& before,
                            const primo::smt::AnyOf& after) {
                           return before.alternatives().front().assertions() ==
                                  after.alternatives().front().assertions();
                         });
  const primo::smt::AnyOf diagnosed =
      assertion_fixed_point(fixture.context, primo::smt::AnyOf::single(primo::smt::Goal{{p}}));
  check(diagnosed.alternatives().front().diagnostic() == "no-progress");
  check(fixture.statistics.get(":tactic.diagnose.invocations") == 1);

  std::size_t observed_changes = 0;
  const primo::smt::Tactic make_true = primo::smt::make_goal_tactic(
      "terminal-change", [](primo::smt::TacticContext&, const primo::smt::Goal& goal) {
        return primo::smt::AnyOf::single(goal.with_assertions({}));
      });
  const primo::smt::Tactic observe_terminal = primo::smt::repeat(
      "observe-terminal", make_true, 4,
      [&observed_changes](const primo::smt::TacticContext&, const primo::smt::AnyOf&,
                          const primo::smt::AnyOf&, std::size_t) {
        ++observed_changes;
        return true;
      });
  const primo::smt::AnyOf terminal =
      observe_terminal(fixture.context, primo::smt::AnyOf::single(primo::smt::Goal{{p}}));
  check(primo::smt::truth(terminal, fixture.context) == primo::smt::GoalTruth::True);
  check(observed_changes == 1);
}

void conditional_uses_a_probe() {
  Fixture fixture;
  const primo::smt::Tactic make_true = primo::smt::make_goal_tactic(
      "make-true", [](primo::smt::TacticContext&, const primo::smt::Goal& goal) {
        return primo::smt::AnyOf::single(goal.with_assertions({}));
      });
  const primo::smt::Tactic choose = primo::smt::conditional(
      "choose",
      [](const primo::smt::TacticContext&, const primo::smt::Goal& goal) {
        return goal.assertions().size() == 1;
      },
      make_true, primo::smt::skip());
  const primo::smt::AnyOf result =
      choose(fixture.context, primo::smt::AnyOf::single(primo::smt::Goal{{fixture.p_term()}}));
  check(primo::smt::truth(result, fixture.context) == primo::smt::GoalTruth::True);
}

void strategy_is_immutable_canonical_and_fingerprinted() {
  Fixture fixture;
  const primo::smt::Tactic identity = primo::smt::make_goal_tactic(
      "identity", [](primo::smt::TacticContext&, const primo::smt::Goal& goal) {
        return primo::smt::AnyOf::single(goal);
      });
  IdentityStrategyParameters parameters{.alpha = "1", .zeta = "2"};
  const primo::smt::Strategy leaf = primo::smt::Strategy::leaf("identity", parameters, identity);
  parameters.zeta = "changed";
  const primo::smt::Strategy strategy = primo::smt::Strategy::sequence(
      {leaf,
       primo::smt::Strategy::conditional(primo::smt::at_most(primo::smt::assertion_count(), 1.0),
                                         primo::smt::Strategy::skip(), leaf)});

  check(leaf.canonical() == "(leaf \"identity\" (\"alpha\" \"1\") (\"zeta\" \"2\"))");
  check(strategy.canonical() ==
        "(sequence (leaf \"identity\" (\"alpha\" \"1\") (\"zeta\" \"2\")) "
        "(conditional \"(<= assertion-count 1)\" skip (leaf \"identity\" "
        "(\"alpha\" \"1\") (\"zeta\" \"2\"))))");
  check(strategy.fingerprint() ==
        primo::smt::Strategy::sequence(
            {leaf, primo::smt::Strategy::conditional(
                       primo::smt::at_most(primo::smt::assertion_count(), 1.0),
                       primo::smt::Strategy::skip(), leaf)})
            .fingerprint());
  check(primo::smt::compile(strategy)(
            fixture.context, primo::smt::AnyOf::single(primo::smt::Goal{{fixture.p_term()}}))
            .size() == 1);
}

void strategy_probe_observes_the_transformed_goal_and_repeat_is_bounded() {
  Fixture fixture;
  const primo::core::TermId p = fixture.p_term();
  std::size_t repeats = 0;
  const primo::smt::Tactic duplicate = primo::smt::make_goal_tactic(
      "duplicate", [p](primo::smt::TacticContext&, const primo::smt::Goal& goal) {
        return primo::smt::AnyOf::single(goal.with_assertions({p, p}));
      });
  const primo::smt::Tactic count = primo::smt::make_goal_tactic(
      "count", [&repeats](primo::smt::TacticContext&, const primo::smt::Goal& goal) {
        ++repeats;
        return primo::smt::AnyOf::single(goal.with_diagnostic(std::to_string(repeats)));
      });
  const primo::smt::Strategy strategy = primo::smt::Strategy::sequence(
      {primo::smt::Strategy::leaf("duplicate", primo::smt::NoStrategyParameters{}, duplicate),
       primo::smt::Strategy::conditional(
           primo::smt::at_most(primo::smt::assertion_count(), 1.0), primo::smt::Strategy::skip(),
           primo::smt::Strategy::repeat(
               primo::smt::Strategy::leaf("count", primo::smt::NoStrategyParameters{}, count),
               3))});
  const primo::smt::AnyOf result = primo::smt::compile(strategy)(
      fixture.context, primo::smt::AnyOf::single(primo::smt::Goal{{p}}));
  check(result.alternatives().front().assertions().size() == 2);
  // Structural equality includes the diagnostic, so all three bounded
  // iterations execute and the transformed assertion count drives the branch.
  check(repeats == 3);
}

void try_for_uses_context_deadlines_without_mutating_the_strategy() {
  Fixture fixture;
  std::size_t applications = 0;
  const primo::smt::Tactic must_not_run = primo::smt::make_goal_tactic(
      "must-not-run", [&applications](primo::smt::TacticContext&, const primo::smt::Goal& goal) {
        ++applications;
        return primo::smt::AnyOf::single(goal);
      });
  const primo::smt::Strategy strategy = primo::smt::Strategy::try_for(
      primo::smt::Strategy::leaf("must-not-run", primo::smt::NoStrategyParameters{}, must_not_run),
      std::chrono::milliseconds(0));
  const std::string fingerprint = strategy.fingerprint();
  const primo::smt::AnyOf result = primo::smt::compile(strategy)(
      fixture.context, primo::smt::AnyOf::single(primo::smt::Goal{{fixture.p_term()}}));
  check(applications == 0);
  check(result.alternatives().front().reason() == primo::smt::InconclusiveReason::ResourceLimit);
  check(strategy.fingerprint() == fingerprint);
  check(!fixture.context.deadline().has_value());
}

void solver_leaf_handles_disjunctive_goals_in_order() {
  Fixture fixture;
  const auto model = std::make_shared<const primo::core::Model>(
      fixture.symbols, std::vector<primo::core::ModelInterpretation>{
                           {fixture.p, {fixture.symbols.bool_sort(), false}, {}}});
  const primo::core::TermId p = fixture.p_term();
  const primo::core::TermId not_p = fixture.terms.mk_not(p);
  std::size_t calls = 0;
  const primo::smt::Tactic solver = primo::smt::make_solver_tactic(
      "alternative-solver", fixture.terms,
      [p, &calls, model](const primo::smt::Goal& goal) -> primo::smt::SolverAnswer {
        ++calls;
        if (goal.assertions().front() == p) {
          return {.status = primo::smt::SolverStatus::Unsat,
                  .reason = primo::smt::InconclusiveReason::None,
                  .diagnostic = {},
                  .typed_model = {}};
        }
        return {.status = primo::smt::SolverStatus::Sat,
                .reason = primo::smt::InconclusiveReason::None,
                .diagnostic = {},
                .typed_model = model};
      });
  const primo::smt::AnyOf sat = solver(
      fixture.context, primo::smt::AnyOf({primo::smt::Goal{{p}}, primo::smt::Goal{{not_p}}}));
  check(calls == 2);
  check(primo::smt::truth(sat, fixture.context) == primo::smt::GoalTruth::True);
  check(sat.alternatives().front().typed_model() == model);

  const primo::smt::AnyOf unsat =
      solver(fixture.context, primo::smt::AnyOf({primo::smt::Goal{{p}}, primo::smt::Goal{{p}}}));
  check(unsat.size() == 1);
  check(primo::smt::truth(unsat, fixture.context) == primo::smt::GoalTruth::False);
}

void experimental_split_bools_materializes_deterministic_goal_product() {
  Fixture fixture;
  const primo::core::SymbolId q_symbol =
      fixture.symbols.declare_constant("q", fixture.symbols.bool_sort());
  const primo::core::TermId p = fixture.p_term();
  const primo::core::TermId q = fixture.terms.mk_constant(q_symbol);
  const primo::core::TermId disjunction = fixture.terms.mk_or({p, q});
  primo::smt::ModelReconstruction reconstruction;
  reconstruction.conversions =
      reconstruction.conversions.appended(primo::smt::BooleanTheoryAssignmentConversion{
          {{.term = p, .value = true, .filtered = false}}});
  const primo::smt::Goal input =
      primo::smt::Goal{{disjunction}}.with_reconstruction(reconstruction);
  const primo::smt::ExperimentalSplitBoolsOptions options{.max_bools = 2};
  const primo::smt::Strategy strategy = primo::smt::Strategy::leaf(
      "experimental-split-bools", options, primo::smt::make_experimental_split_bools(options));

  const primo::smt::AnyOf result =
      primo::smt::compile(strategy)(fixture.context, primo::smt::AnyOf::single(input));
  check(result.size() == 4);
  const primo::core::TermId not_p = fixture.terms.mk_not(p);
  const primo::core::TermId not_q = fixture.terms.mk_not(q);
  check(result.alternatives()[0].assertions() ==
        std::vector<primo::core::TermId>({disjunction, not_p, not_q}));
  check(result.alternatives()[1].assertions() ==
        std::vector<primo::core::TermId>({disjunction, not_p, q}));
  check(result.alternatives()[2].assertions() ==
        std::vector<primo::core::TermId>({disjunction, p, not_q}));
  check(result.alternatives()[3].assertions() ==
        std::vector<primo::core::TermId>({disjunction, p, q}));
  for (const primo::smt::Goal& alternative : result.alternatives()) {
    check(alternative.reconstruction() == reconstruction);
  }
  check(strategy.canonical() == "(leaf \"experimental-split-bools\" (\"k\" \"2\"))");
  check(fixture.statistics.get(":experimental.split-bools.input-goals") == 1);
  check(fixture.statistics.get(":experimental.split-bools.selected") == 2);
  check(fixture.statistics.get(":experimental.split-bools.output-goals") == 4);
}

void experimental_split_bools_prioritizes_symbols_and_bounds_materialization() {
  Fixture fixture;
  const primo::core::SymbolId x_symbol =
      fixture.symbols.declare_constant("x", fixture.symbols.real_sort());
  const primo::core::TermId p = fixture.p_term();
  const primo::core::TermId x = fixture.terms.mk_constant(x_symbol);
  const primo::core::TermId bound = fixture.terms.mk_lt(x, fixture.terms.mk_rational(0));
  const primo::core::TermId conjunction = fixture.terms.mk_and({bound, p});
  const primo::smt::Tactic split = primo::smt::make_experimental_split_bools({.max_bools = 1});

  const primo::smt::AnyOf result =
      split(fixture.context, primo::smt::AnyOf::single(primo::smt::Goal{{conjunction}}));
  check(result.size() == 2);
  check(result.alternatives()[0].assertions().back() == fixture.terms.mk_not(p));
  check(result.alternatives()[1].assertions().back() == p);

  const primo::smt::AnyOf all_available =
      primo::smt::make_experimental_split_bools({.max_bools = 3})(
          fixture.context, primo::smt::AnyOf::single(primo::smt::Goal{{conjunction}}));
  check(all_available.size() == 4);

  bool rejected = false;
  try {
    static_cast<void>(primo::smt::make_experimental_split_bools(
        {.max_bools = primo::smt::kMaxExperimentalSplitBools + 1}));
  } catch (const std::invalid_argument&) {
    rejected = true;
  }
  check(rejected);
}

void experimental_split_bools_uses_sequential_first_sat_execution_and_keeps_model() {
  primo::smt::CheckSatOptions options;
  options.preprocessing_mode = primo::smt::PreprocessingMode::Off;
  options.solved_constant_substitution = false;
  options.experimental_split_bools = 2;
  options.produce_model = true;
  const primo::smt::CheckSatOutcome result = primo::smt::check_sat_smtlib(R"(
    (set-logic QF_UF)
    (set-option :produce-models true)
    (declare-fun p () Bool)
    (declare-fun q () Bool)
    (assert (or p q))
    (check-sat)
  )",
                                                                          options);
  check(result.result == primo::smt::CheckSatResult::Sat);
  check(result.statistics.get(":experimental.split-bools.selected") == 2);
  check(result.statistics.get(":experimental.split-bools.output-goals") == 4);
  check(result.statistics.get(":tactic.solve-boolean.invocations") == 2);
  check(result.model.find("(define-fun p () Bool false)") != std::string::npos);
  check(result.model.find("(define-fun q () Bool true)") != std::string::npos);
}

void sequential_scheduler_tries_each_arm_from_the_original_goal() {
  Fixture fixture;
  const primo::core::TermId p = fixture.p_term();
  const primo::smt::Tactic inconclusive = primo::smt::make_goal_tactic(
      "inconclusive", [](primo::smt::TacticContext&, const primo::smt::Goal& goal) {
        return primo::smt::AnyOf::single(goal.with_diagnostic("budget"));
      });
  const primo::smt::Tactic solve = primo::smt::make_goal_tactic(
      "solve", [p](primo::smt::TacticContext&, const primo::smt::Goal& goal) {
        check(goal.assertions() == std::vector<primo::core::TermId>{p});
        check(goal.diagnostic().empty());
        return primo::smt::AnyOf::single(goal.with_assertions({}));
      });
  const auto scheduler = std::make_shared<primo::smt::SequentialTacticScheduler>();
  const primo::smt::Tactic portfolio =
      primo::smt::first_terminal("portfolio", scheduler, {inconclusive, solve});
  const primo::smt::AnyOf result =
      portfolio(fixture.context, primo::smt::AnyOf::single(primo::smt::Goal{{fixture.p_term()}}));
  check(primo::smt::truth(result, fixture.context) == primo::smt::GoalTruth::True);
}

void cancellation_stops_before_running_a_tactic() {
  Fixture fixture;
  std::size_t applications = 0;
  primo::smt::TacticContext cancelled_context(fixture.terms, fixture.statistics,
                                              [] { return true; });
  const primo::smt::Tactic tactic = primo::smt::make_goal_tactic(
      "must-not-run", [&applications](primo::smt::TacticContext&, const primo::smt::Goal& goal) {
        ++applications;
        return primo::smt::AnyOf::single(goal);
      });

  const primo::smt::AnyOf result =
      tactic(cancelled_context, primo::smt::AnyOf::single(primo::smt::Goal{{fixture.p_term()}}));
  check(applications == 0);
  check(result.alternatives().front().reason() == primo::smt::InconclusiveReason::Cancelled);
  check(result.alternatives().front().diagnostic() == "cancelled");
}

// Cancellation that arrives while a repeat is already running is observed by
// the child tactic, not by the loop. The loop must still converge promptly
// instead of spinning to its iteration bound.
// An AnyOf is disjunctive. A preprocessed result reduced to its first
// alternative loses satisfiability that only a later alternative carries, so
// the session triages the whole disjunction rather than reading .front().
// canonical() backs operator== and fingerprint(), and compile() lowers the
// same tree. Strategies are built programmatically, so nothing bounds their
// depth and neither walk may recurse: a recursive canonical() overflowed the
// stack between depth 5000 and 20000 on this workstation.
//
// The two are checked at different depths on purpose. Compiling produces
// nested closures, and destroying, and running, that chain recurses once per
// level regardless of how it was built, which caps the compiled depth well
// below the rendered one. Making execution and teardown iterative would be a
// different change -- a tactic interpreter rather than composed closures --
// and is recorded in docs/debugging.md instead. 12000 is chosen because the
// recursive canonical() overflowed there in both the Release and the
// sanitizer build, while the iterative one clears both.
void deeply_nested_strategies_canonicalize_without_recursion() {
  constexpr std::size_t kDeep = 12000;
  primo::smt::Strategy nest = primo::smt::Strategy::skip();
  for (std::size_t depth = 0; depth < kDeep; ++depth) {
    nest = primo::smt::Strategy::sequence({nest});
  }

  const std::string canonical = nest.canonical();
  // Each level wraps the innermost "skip" in "(sequence " and ")".
  check(canonical.size() == kDeep * 11 + 4);
  check(canonical.starts_with("(sequence (sequence "));
  // The innermost leaf sits after one "(sequence " opener per level.
  check(canonical.compare(kDeep * 10, 4, "skip") == 0);
  check(nest.fingerprint() == nest.fingerprint());
  check(nest == nest);
}

void nested_strategies_compile_to_a_working_pipeline() {
  constexpr std::size_t kDepth = 2000;
  primo::smt::Strategy nest = primo::smt::Strategy::skip();
  for (std::size_t depth = 0; depth < kDepth; ++depth) {
    nest = primo::smt::Strategy::sequence({nest});
  }

  Fixture fixture;
  const primo::smt::Tactic pipeline = compile(nest);
  const primo::smt::AnyOf result =
      pipeline(fixture.context, primo::smt::AnyOf::single(primo::smt::Goal{{fixture.p_term()}}));
  check(result.alternatives().front().assertions() == std::vector{fixture.p_term()});
}

void triage_keeps_a_satisfiable_alternative_when_the_first_is_false() {
  Fixture fixture;
  const primo::core::TermId contradiction = fixture.terms.mk_bool(false);
  const primo::smt::AnyOf preprocessed(std::vector<primo::smt::Goal>{
      primo::smt::Goal{{contradiction}}, primo::smt::Goal{{fixture.p_term()}}});

  const primo::smt::PreprocessedAlternatives triaged =
      primo::smt::triage_preprocessed_alternatives(fixture.terms, preprocessed);

  check(!triaged.any_inconclusive);
  check(triaged.surviving.size() == 1);
  check(triaged.surviving.front().assertions() == std::vector{fixture.p_term()});
}

void triage_reports_unsat_only_when_every_alternative_is_false() {
  Fixture fixture;
  const primo::core::TermId contradiction = fixture.terms.mk_bool(false);
  const primo::smt::AnyOf all_false(std::vector<primo::smt::Goal>{
      primo::smt::Goal{{contradiction}}, primo::smt::Goal{{fixture.p_term(), contradiction}}});

  const primo::smt::PreprocessedAlternatives triaged =
      primo::smt::triage_preprocessed_alternatives(fixture.terms, all_false);

  check(!triaged.any_inconclusive);
  check(triaged.surviving.empty());
}

// An interrupted alternative cannot simply be dropped: it might have been the
// only satisfiable disjunct, so the whole result is inconclusive.
void triage_flags_any_inconclusive_alternative() {
  Fixture fixture;
  const primo::smt::AnyOf partly_interrupted(std::vector<primo::smt::Goal>{
      primo::smt::Goal{{fixture.p_term()}},
      primo::smt::Goal{{fixture.p_term()}}.with_diagnostic(
          primo::smt::InconclusiveReason::ResourceLimit, "deadline reached")});

  const primo::smt::PreprocessedAlternatives triaged =
      primo::smt::triage_preprocessed_alternatives(fixture.terms, partly_interrupted);

  check(triaged.any_inconclusive);
  check(triaged.surviving.size() == 1);
}

// A single conclusive alternative, the shape every production preprocessing
// strategy produces today, passes through untouched.
void triage_passes_a_single_alternative_through() {
  Fixture fixture;
  const primo::smt::AnyOf single = primo::smt::AnyOf::single(primo::smt::Goal{{fixture.p_term()}});

  const primo::smt::PreprocessedAlternatives triaged =
      primo::smt::triage_preprocessed_alternatives(fixture.terms, single);

  check(!triaged.any_inconclusive);
  check(triaged.surviving.size() == 1);
  check(triaged.surviving.front().assertions() == std::vector{fixture.p_term()});
}

void cancellation_during_a_repeat_terminates_it_promptly() {
  Fixture fixture;
  bool cancelled = false;
  primo::smt::TacticContext context(fixture.terms, fixture.statistics,
                                    [&cancelled] { return cancelled; });
  std::size_t body_applications = 0;
  std::size_t observations = 0;
  const primo::smt::Tactic body = primo::smt::make_goal_tactic(
      "repeat-body",
      [&body_applications, &cancelled](primo::smt::TacticContext&, const primo::smt::Goal& goal) {
        ++body_applications;
        // The third pass is interrupted from outside, mid-run.
        if (body_applications == 3) {
          cancelled = true;
        }
        return primo::smt::AnyOf::single(goal.with_diagnostic(primo::smt::InconclusiveReason::None,
                                                              std::to_string(body_applications)));
      });
  const primo::smt::Tactic repeated = primo::smt::repeat(
      "cancellable-repeat", body, 4096, {}, {},
      [&observations](const primo::smt::TacticContext&, const primo::smt::AnyOf&,
                      const primo::smt::AnyOf&, std::size_t) { ++observations; });

  const primo::smt::AnyOf result =
      repeated(context, primo::smt::AnyOf::single(primo::smt::Goal{{fixture.p_term()}}));

  // Three productive passes, then two interrupted ones: the first records the
  // cancellation, the second reproduces it and is recognised as the fixed
  // point. Nowhere near the 4096 bound.
  check(body_applications == 3);
  check(observations == 5);
  check(result.alternatives().front().reason() == primo::smt::InconclusiveReason::Cancelled);
}

// A context that is already cancelled short-circuits at the outer tactic, so
// neither the loop body nor its observer runs at all.
void cancellation_before_a_repeat_runs_nothing_inside_it() {
  Fixture fixture;
  primo::smt::TacticContext cancelled_context(fixture.terms, fixture.statistics,
                                              [] { return true; });
  std::size_t observations = 0;
  std::size_t body_applications = 0;
  const primo::smt::Tactic body = primo::smt::make_goal_tactic(
      "repeat-body",
      [&body_applications](primo::smt::TacticContext&, const primo::smt::Goal& goal) {
        ++body_applications;
        return primo::smt::AnyOf::single(goal);
      });
  const primo::smt::Tactic repeated = primo::smt::repeat(
      "cancellable-repeat", body, 64, {}, {},
      [&observations](const primo::smt::TacticContext&, const primo::smt::AnyOf&,
                      const primo::smt::AnyOf&, std::size_t) { ++observations; });

  const primo::smt::AnyOf result =
      repeated(cancelled_context, primo::smt::AnyOf::single(primo::smt::Goal{{fixture.p_term()}}));

  check(body_applications == 0);
  check(observations == 0);
  check(result.alternatives().front().reason() == primo::smt::InconclusiveReason::Cancelled);
}

void check_sat_honors_an_existing_cancellation_request() {
  primo::smt::CheckSatOptions options;
  options.cancelled = [] { return true; };
  const auto result = primo::smt::check_sat_smtlib("(check-sat)", options);
  check(result.result == primo::smt::CheckSatResult::Unknown);
  check(result.message == "cancelled");
  check(result.reason == primo::smt::InconclusiveReason::Cancelled);
}

void check_sat_preserves_cancellation_observed_by_preprocessing() {
  for (const std::string_view input :
       {std::string_view{"(check-sat)"}, std::string_view{"(assert false) (check-sat)"}}) {
    std::size_t cancellation_probes = 0;
    primo::smt::CheckSatOptions options;
    options.cancelled = [&cancellation_probes] { return cancellation_probes++ > 0; };
    const primo::smt::CheckSatOutcome result = primo::smt::check_sat_smtlib(input, options);
    check(result.result == primo::smt::CheckSatResult::Unknown);
    check(result.message == "cancelled");
    check(result.reason == primo::smt::InconclusiveReason::Cancelled);
  }
}

void frontend_type_errors_are_reported_as_unsupported() {
  const primo::smt::CheckSatOutcome result = primo::smt::check_sat_smtlib(R"(
    (set-logic QF_LRA)
    (assert (+ true true))
    (check-sat)
  )");
  check(result.result == primo::smt::CheckSatResult::Unsupported);
  check(result.reason == primo::smt::InconclusiveReason::Unsupported);
  check(result.message == "expected Real or Int term");
}

void solver_dispatch_gives_cancellation_precedence_across_goals() {
  Fixture fixture;
  std::size_t calls = 0;
  const primo::smt::Tactic scripted = primo::smt::make_goal_tactic(
      "scripted", [&calls](primo::smt::TacticContext&, const primo::smt::Goal& goal) {
        ++calls;
        if (calls == 1) {
          return primo::smt::AnyOf::single(goal.with_diagnostic(
              primo::smt::InconclusiveReason::ResourceLimit, "budget exhausted"));
        }
        return primo::smt::AnyOf::single(
            goal.with_diagnostic(primo::smt::InconclusiveReason::Cancelled, "cancelled"));
      });
  const primo::smt::SolverTacticSet solvers{.boolean_solver = scripted,
                                            .euf_solver = scripted,
                                            .lra_solver = scripted,
                                            .lra_model_only_solver = scripted,
                                            .mixed_solver = scripted,
                                            .lia_solver = scripted,
                                            .uflia_solver = scripted};
  primo::smt::SolverStrategyOptions options;
  options.logic = "QF_LIA";
  const primo::smt::Tactic dispatch = primo::smt::make_solver_dispatch(options, solvers);
  const primo::smt::Goal goal{{fixture.p_term()}};

  const primo::smt::AnyOf produced = dispatch(fixture.context, primo::smt::AnyOf({goal, goal}));
  check(calls == 2);
  check(produced.size() == 1);
  check(produced.alternatives().front().reason() == primo::smt::InconclusiveReason::Cancelled);
  check(primo::smt::outcome_from_tactic_result(produced, fixture.context).reason ==
        primo::smt::InconclusiveReason::Cancelled);

  const primo::smt::AnyOf diagnosed = dispatch(
      fixture.context,
      primo::smt::AnyOf(
          {goal.with_diagnostic(primo::smt::InconclusiveReason::Unsupported, "unsupported"),
           goal.with_diagnostic(primo::smt::InconclusiveReason::Cancelled, "cancelled")}));
  check(diagnosed.size() == 1);
  check(diagnosed.alternatives().front().reason() == primo::smt::InconclusiveReason::Cancelled);
}

void solver_adapter_types_expected_inconclusive_results() {
  Fixture fixture;
  const primo::smt::Tactic unsupported = primo::smt::make_solver_tactic(
      "unsupported", fixture.terms, [](const primo::smt::Goal&) -> primo::smt::SolverAnswer {
        throw primo::smt::UnsupportedInput("outside fragment");
      });
  const primo::smt::AnyOf result =
      unsupported(fixture.context, primo::smt::AnyOf::single(primo::smt::Goal{{fixture.p_term()}}));

  check(result.alternatives().front().reason() == primo::smt::InconclusiveReason::Unsupported);
  const primo::smt::CheckSatOutcome outcome =
      primo::smt::outcome_from_tactic_result(result, fixture.context);
  check(outcome.result == primo::smt::CheckSatResult::Unsupported);
  check(outcome.message == "outside fragment");
  check(outcome.reason == primo::smt::InconclusiveReason::Unsupported);
}

void typed_inconclusive_reason_overrides_terminal_formula_truth() {
  Fixture fixture;
  for (const primo::smt::Goal& terminal :
       {primo::smt::Goal{}, primo::smt::Goal{{fixture.terms.mk_bool(false)}}}) {
    const primo::smt::AnyOf cancelled = primo::smt::AnyOf::single(
        terminal.with_diagnostic(primo::smt::InconclusiveReason::Cancelled, "cancelled"));
    const primo::smt::CheckSatOutcome outcome =
        primo::smt::outcome_from_tactic_result(cancelled, fixture.context);
    check(outcome.result == primo::smt::CheckSatResult::Unknown);
    check(outcome.message == "cancelled");
    check(outcome.reason == primo::smt::InconclusiveReason::Cancelled);
  }
}

void preprocessing_fixed_point_and_terminal_change_statistics_are_exact() {
  primo::smt::CheckSatOptions options;
  options.preprocessing_passes = 4;

  const primo::smt::CheckSatOutcome stable = primo::smt::check_sat_smtlib(R"(
    (set-logic QF_UF)
    (declare-sort A 0)
    (declare-fun a () A)
    (declare-fun b () A)
    (assert (not (= a b)))
    (check-sat)
  )",
                                                                          options);
  check(stable.result == primo::smt::CheckSatResult::Sat);
  check(stable.statistics.get(primo::util::StatKey::PreprocessingPasses) == 1);
  check(stable.statistics.get(primo::util::StatKey::PreprocessingChangedPasses) == 0);
  check(stable.statistics.get(":tactic.preprocessing-pass.invocations") == 1);
  check(stable.statistics.get(":tactic.preprocessing-round.invocations") == 1);
  check(stable.statistics.get(":tactic.forced-atom-propagation.invocations") == 1);

  const primo::smt::CheckSatOutcome contradiction = primo::smt::check_sat_smtlib(R"(
    (set-logic QF_UF)
    (declare-sort A 0)
    (declare-fun f (A A) A)
    (declare-fun a () A)
    (declare-fun b () A)
    (declare-fun c () A)
    (declare-fun d () A)
    (assert (= a b))
    (assert (= c d))
    (assert (not (= (f a c) (f b d))))
    (check-sat)
  )",
                                                                                 options);
  check(contradiction.result == primo::smt::CheckSatResult::Unsat);
  check(contradiction.statistics.get(primo::util::StatKey::PreprocessingPasses) == 1);
  check(contradiction.statistics.get(primo::util::StatKey::PreprocessingChangedPasses) == 1);
  check(contradiction.statistics.get(primo::util::StatKey::PreprocessingAdaptivePasses) == 0);
  check(contradiction.statistics.get(":tactic.preprocessing-pass.invocations") == 1);

  const primo::smt::CheckSatOutcome early_contradiction = primo::smt::check_sat_smtlib(R"(
    (set-logic QF_UFLRA)
    (declare-fun p () Bool)
    (declare-fun q () Bool)
    (declare-fun g (Bool Bool) Bool)
    (declare-fun x () Real)
    (assert (< 0 x))
    (assert (=> (g q p) (not (g q p))))
    (assert (= (<= (- x) (- x)) (or (g q p) (g q p))))
    (check-sat)
  )",
                                                                                       options);
  check(early_contradiction.result == primo::smt::CheckSatResult::Unsat);
  check(early_contradiction.statistics.get(primo::util::StatKey::GuardedRealEqualityDistributed) ==
        0);
  check(early_contradiction.statistics.get(":no.purification.definitions") == 0);
  check(early_contradiction.statistics.get(":no.purification.skipped-no-sites") == 1);
  check(early_contradiction.statistics.get(":preprocessing.boolean-flatten-post.rewrites") == 0);
}

void solver_adapter_does_not_hide_serious_failures() {
  Fixture fixture;
  const primo::smt::Tactic broken = primo::smt::make_solver_tactic(
      "broken", fixture.terms, [](const primo::smt::Goal&) -> primo::smt::SolverAnswer {
        throw std::runtime_error("backend invariant");
      });
  bool threw = false;
  try {
    static_cast<void>(
        broken(fixture.context, primo::smt::AnyOf::single(primo::smt::Goal{{fixture.p_term()}})));
  } catch (const std::runtime_error&) {
    threw = true;
  }
  check(threw);
}

void preprocessing_round_transforms_each_alternative_independently() {
  Fixture fixture;
  const primo::core::TermId p = fixture.p_term();
  const primo::core::TermId not_p = fixture.terms.mk_not(p);
  primo::smt::ModelReconstruction true_reconstruction;
  true_reconstruction.conversions =
      true_reconstruction.conversions.appended(primo::smt::BooleanTheoryAssignmentConversion{
          {{.term = p, .value = true, .filtered = false}}});
  primo::smt::ModelReconstruction false_reconstruction;
  false_reconstruction.conversions =
      false_reconstruction.conversions.appended(primo::smt::BooleanTheoryAssignmentConversion{
          {{.term = p, .value = false, .filtered = false}}});
  const primo::smt::Tactic round =
      primo::smt::make_preprocessing_round(primo::smt::PreprocessingRoundOptions{});

  const primo::smt::AnyOf result = round(
      fixture.context,
      primo::smt::AnyOf({primo::smt::Goal{{p}}.with_reconstruction(true_reconstruction),
                         primo::smt::Goal{{not_p}}.with_reconstruction(false_reconstruction)}));
  check(result.size() == 2);
  const auto true_assignments =
      only_boolean_assignments(result.alternatives()[0].reconstruction().conversions);
  const auto false_assignments =
      only_boolean_assignments(result.alternatives()[1].reconstruction().conversions);
  check(true_assignments.size() == 1);
  check(false_assignments.size() == 1);
  check(true_assignments.front().value);
  check(!false_assignments.front().value);
  check(fixture.statistics.get(":tactic.preprocessing-round.invocations") == 1);
  check(fixture.statistics.get(":tactic.boolean-unit-propagation.invocations") == 1);
  check(fixture.statistics.get(":tactic.forced-atom-propagation.invocations") == 1);
  check(fixture.statistics.get(":tactic.unit-equality-propagation.invocations") == 1);
}

bool assertions_mention(const primo::core::TermManager& terms,
                        const std::vector<primo::core::TermId>& roots, primo::core::TermId target) {
  auto pending = roots;
  while (!pending.empty()) {
    const auto id = pending.back();
    pending.pop_back();
    if (id == target) return true;
    const auto& children = terms.get(id).children;
    pending.insert(pending.end(), children.begin(), children.end());
  }
  return false;
}

void finite_domain_snapshot_is_bound_to_the_executing_goal() {
  Fixture fixture;
  const auto integer = fixture.symbols.int_sort();
  const auto a =
      fixture.terms.mk_constant(fixture.symbols.declare_constant("eliminated_a", integer));
  const auto b = fixture.terms.mk_constant(fixture.symbols.declare_constant("live_b", integer));
  const auto f = fixture.symbols.declare_function("domain_f", {integer}, integer);
  const auto zero = fixture.terms.mk_rational(primo::util::Rational(0), integer);
  const auto one = fixture.terms.mk_rational(primo::util::Rational(1), integer);
  const auto seven = fixture.terms.mk_rational(primo::util::Rational(7), integer);
  const auto domain = [&](primo::core::TermId argument) {
    const auto application = fixture.terms.mk_app(f, {argument});
    const auto low = fixture.terms.mk_eq(application, zero);
    const auto high = fixture.terms.mk_eq(application, one);
    return fixture.terms.mk_or({low, high});
  };
  const std::vector original{fixture.terms.mk_eq(a, seven), domain(a)};
  primo::smt::FiniteDomainAtMostOne generator(fixture.terms);
  const auto raw =
      std::make_shared<const primo::smt::FiniteDomainAtMostOneResult>(generator.generate(original));
  check(!raw->clauses.empty());
  const auto tactic = primo::smt::make_finite_domain_preprocess(
      {.generate_at_most_one = true},
      {.source_assertions = original, .raw_at_most_one = raw, .analyzed_symmetry = std::nullopt});
  const auto unchanged =
      tactic(fixture.context, primo::smt::AnyOf::single(primo::smt::Goal(original)));
  check(assertions_mention(fixture.terms, unchanged.alternatives()[0].assertions(), a));
  check(fixture.statistics.get(":preprocessing.finite-domain.raw-cache-hits") == 1);
  // One shared tactic can see alternatives with different surviving symbols.
  // Neither alternative may regain a constant removed by solve-eqs.
  const auto changed =
      tactic(fixture.context,
             primo::smt::AnyOf({primo::smt::Goal({domain(seven)}), primo::smt::Goal({domain(b)})}));
  check(changed.size() == 2);
  for (const auto& goal : changed.alternatives())
    check(!assertions_mention(fixture.terms, goal.assertions(), a));
  check(fixture.statistics.get(":preprocessing.finite-domain.raw-cache-refreshes") == 2);
  check(assertions_mention(fixture.terms, changed.alternatives()[1].assertions(), b));
}

void symmetry_snapshot_is_bound_to_the_executing_goal() {
  Fixture fixture;
  const auto sort = fixture.symbols.declare_sort("SymmetricValues");
  const auto constant = [&](const char* name) {
    return fixture.terms.mk_constant(fixture.symbols.declare_constant(name, sort));
  };
  const auto first = constant("first");
  const auto second = constant("second");
  const auto old = constant("old_choice");
  const auto live = constant("live_choice");
  const auto different = fixture.terms.mk_not(fixture.terms.mk_eq(first, second));
  const auto choice = [&](primo::core::TermId term) {
    const auto left = fixture.terms.mk_eq(term, first);
    const auto right = fixture.terms.mk_eq(term, second);
    return fixture.terms.mk_or({left, right});
  };
  const std::vector original{different, choice(old)};
  primo::smt::UfSymmetryBreaker breaker(fixture.terms);
  const auto analyzed = breaker.break_symmetries(original);
  check(!analyzed.constraints.empty());
  const auto tactic = primo::smt::make_finite_domain_preprocess(
      {.symmetry_breaking = true}, {.source_assertions = original, .analyzed_symmetry = analyzed});
  static_cast<void>(tactic(fixture.context, primo::smt::AnyOf::single(primo::smt::Goal(original))));
  check(fixture.statistics.get(":preprocessing.symmetry.cache-hits") == 1);
  const auto changed = tactic(
      fixture.context, primo::smt::AnyOf::single(primo::smt::Goal({different, choice(live)})));
  check(!assertions_mention(fixture.terms, changed.alternatives()[0].assertions(), old));
  check(assertions_mention(fixture.terms, changed.alternatives()[0].assertions(), live));
  check(fixture.statistics.get(":preprocessing.symmetry.cache-refreshes") == 1);
}

void public_preprocessing_leaves_are_independently_constructible_and_typed() {
  Fixture fixture;
  const primo::core::TermId p = fixture.p_term();
  const primo::core::TermId nested = fixture.terms.mk_and({p, fixture.terms.mk_and({p, p})});

  const primo::smt::NormalizeBooleanOptions boolean_options{
      .stage = primo::smt::BooleanNormalizationStage::Initial,
      .flatten_threshold = 8,
      .negation_mode = primo::smt::BooleanNegationRewriteMode::NegationNormalForm,
      .implication_mode = primo::smt::BooleanImpliesRewriteMode::Off};
  const primo::smt::Strategy boolean_strategy = primo::smt::Strategy::leaf(
      "normalize-boolean", boolean_options, primo::smt::make_normalize_boolean(boolean_options));
  const primo::smt::AnyOf normalized = primo::smt::compile(boolean_strategy)(
      fixture.context, primo::smt::AnyOf::single(primo::smt::Goal{{nested}}));
  check(normalized.alternatives().front().assertions().size() == 1);
  check(boolean_strategy.canonical().find("flatten-threshold") != std::string::npos);
  check(boolean_strategy.canonical().find("\"negation-mode\" \"nnf\"") != std::string::npos);

  const primo::smt::AnyOf propagated = primo::smt::make_propagate_values()(
      fixture.context, primo::smt::AnyOf::single(primo::smt::Goal{{p}}));
  check(propagated.size() == 1);
  check(fixture.statistics.get(":tactic.propagate-values.invocations") == 1);

  const primo::smt::SolveEqsOptions solve_options{.solved_constant_substitution = true,
                                                  .gaussian_elimination = false,
                                                  .use_ground_unit_substitution = false,
                                                  .use_guarded_ground_unit_substitution = false};
  static_cast<void>(primo::smt::make_solve_eqs(solve_options)(
      fixture.context, primo::smt::AnyOf::single(primo::smt::Goal{{p}})));
  check(fixture.statistics.get(":tactic.solve-eqs.invocations") == 1);

  static_cast<void>(
      primo::smt::make_derive_equalities({.equality_bridge = true, .unit_consequences = true})(
          fixture.context, primo::smt::AnyOf::single(primo::smt::Goal{{p}})));
  check(fixture.statistics.get(":tactic.derive-equalities.invocations") == 1);

  static_cast<void>(primo::smt::make_finite_domain_preprocess(
      {.evaluate_equalities = true, .generate_ground_congruence = true})(
      fixture.context, primo::smt::AnyOf::single(primo::smt::Goal{{p}})));
  check(fixture.statistics.get(":tactic.finite-domain-preprocess.invocations") == 1);

  const primo::core::SymbolId x_symbol =
      fixture.symbols.declare_constant("x", fixture.symbols.real_sort());
  const primo::core::TermId x = fixture.terms.mk_constant(x_symbol);
  const primo::core::TermId bound = fixture.terms.mk_le(x, fixture.terms.mk_rational(0));
  primo::smt::TacticContext model_context(fixture.terms, fixture.statistics, {}, true);
  const primo::smt::AnyOf eliminated = primo::smt::make_eliminate_monotone()(
      model_context, primo::smt::AnyOf::single(primo::smt::Goal{{bound}}));
  check(eliminated.alternatives().front().reconstruction().conversions.size() <= 1);
  check(fixture.statistics.get(":tactic.eliminate-monotone.invocations") == 1);

  const primo::smt::NormalizeArithmeticOptions arithmetic_options{
      .stage = primo::smt::ArithmeticNormalizationStage::Catchup,
      .canonicalize = true,
      .lower_real_equalities = true,
      .guarded_real_equality_lowering = false};
  static_cast<void>(primo::smt::make_normalize_arithmetic(arithmetic_options)(
      model_context, primo::smt::AnyOf::single(primo::smt::Goal{{bound}})));
  check(fixture.statistics.get(":tactic.normalize-arithmetic.invocations") == 1);

  primo::smt::Frontend frontend;
  primo::util::Statistics purification_statistics;
  primo::smt::TacticContext purification_context(
      frontend.terms(), purification_statistics, {}, false,
      [&frontend](primo::core::SortId sort, std::string_view prefix) {
        return frontend.fresh_internal_constant(sort, prefix);
      });
  const primo::core::TermId trivial_bound =
      frontend.terms().mk_le(frontend.terms().mk_rational(0), frontend.terms().mk_rational(1));
  static_cast<void>(primo::smt::make_purify_mixed(
      primo::smt::PurifyMixedOptions{.canonicalize_arithmetic_after = true})(
      purification_context, primo::smt::AnyOf::single(primo::smt::Goal{{trivial_bound}})));
  check(purification_statistics.get(":tactic.purify-mixed.invocations") == 1);
  check(purification_statistics.get(":no.purification.skipped-no-sites") == 1);
}

void production_preprocessing_profile_and_strategy_are_explicit_and_stable() {
  primo::smt::Frontend frontend;
  primo::core::TermManager& terms = frontend.terms();
  const primo::core::TermId bound = terms.mk_le(terms.mk_rational(0), terms.mk_rational(1));
  primo::smt::CheckSatOptions options;
  options.preprocessing_mode = primo::smt::PreprocessingMode::Strong;
  options.preprocessing_passes = 1;
  options.preprocessing_adaptive_extra_passes = 2;
  options.monotone_elimination = true;
  primo::util::Statistics statistics;
  const primo::smt::PreprocessingProfile profile =
      primo::smt::analyze_preprocessing_profile(terms, {bound}, options, false);
  check(profile.kind == primo::smt::PreprocessingProfileKind::Strong);
  check(profile.strong_preprocessing);
  check(profile.preprocessing_passes == 3);
  const primo::smt::Strategy strategy =
      primo::smt::make_preprocessing_strategy(options, profile, {bound});
  const std::string canonical = strategy.canonical();
  const std::size_t profile_position = canonical.find("preprocessing-profile");
  const std::size_t repeat_position = canonical.find("adaptive-reachable-term-shrink");
  const std::size_t finite_position = canonical.find("finite-domain-preprocess");
  const std::size_t arithmetic_position = canonical.find("normalize-arithmetic");
  const std::size_t monotone_position = canonical.find("eliminate-monotone");
  const std::size_t purify_position = canonical.find("purify-mixed");
  const std::size_t boolean_position = canonical.find("normalize-boolean");
  check(profile_position < repeat_position);
  check(repeat_position < finite_position);
  check(finite_position < arithmetic_position);
  check(arithmetic_position < monotone_position);
  check(monotone_position < purify_position);
  check(purify_position < boolean_position);
  check(canonical.find("(repeat 5 \"adaptive-reachable-term-shrink\"") != std::string::npos);
  check(canonical.find("\"equality-definitions\" \"true\"") != std::string::npos);
  check(strategy.fingerprint() == strategy.fingerprint());

  primo::smt::CheckSatOptions bypass_options;
  bypass_options.preprocessing_mode = primo::smt::PreprocessingMode::Auto;
  bypass_options.auto_preprocess_tuning.large_conjunct_limit = 1;
  const primo::smt::PreprocessingProfile bypass =
      primo::smt::analyze_preprocessing_profile(terms, {bound, bound}, bypass_options, false);
  check(bypass.kind == primo::smt::PreprocessingProfileKind::LargeInputBypass);
  check(!bypass.preprocessing_loop_enabled);
}

void solver_dispatch_uses_formula_tactics() {
  primo::smt::CheckSatOptions options;
  options.preprocessing_mode = primo::smt::PreprocessingMode::Off;
  options.solved_constant_substitution = false;

  const auto boolean_result = primo::smt::check_sat_smtlib(R"(
    (set-logic QF_UF)
    (declare-fun p () Bool)
    (assert p)
    (check-sat)
  )",
                                                           options);
  check(boolean_result.result == primo::smt::CheckSatResult::Sat);
  check(boolean_result.statistics.get(":tactic.solver-dispatch.invocations") == 1);
  check(boolean_result.statistics.get(":tactic.solve-boolean.invocations") == 1);

  const auto euf_result = primo::smt::check_sat_smtlib(R"(
    (set-logic QF_UF)
    (declare-sort U 0)
    (declare-fun a () U)
    (declare-fun b () U)
    (declare-fun f (U) U)
    (assert (= (f a) (f b)))
    (check-sat)
  )",
                                                       options);
  check(euf_result.result == primo::smt::CheckSatResult::Sat);
  check(euf_result.statistics.get(":tactic.solve-qf-uf.invocations") == 1);

  const auto lra_result = primo::smt::check_sat_smtlib(R"(
    (set-logic QF_LRA)
    (declare-fun x () Real)
    (assert (< x 0))
    (check-sat)
  )",
                                                       options);
  check(lra_result.result == primo::smt::CheckSatResult::Sat);
  check(lra_result.statistics.get(":tactic.solve-qf-lra.invocations") == 1);

  const auto mixed_result = primo::smt::check_sat_smtlib(R"(
    (set-logic QF_UFLRA)
    (declare-fun x () Real)
    (declare-fun f (Real) Real)
    (assert (< x 0))
    (assert (= (f x) x))
    (check-sat)
  )",
                                                         options);
  check(mixed_result.result == primo::smt::CheckSatResult::Sat);
  check(mixed_result.statistics.get(":tactic.solve-qf-uflra.invocations") == 1);

  const auto lia_result = primo::smt::check_sat_smtlib(R"(
    (set-logic QF_LIA)
    (declare-fun x () Int)
    (assert (< x 0))
    (check-sat)
  )",
                                                       options);
  check(lia_result.result == primo::smt::CheckSatResult::Sat);
  check(lia_result.statistics.get(":tactic.solve-qf-lia.invocations") == 1);

  primo::smt::CheckSatOptions model_options = options;
  model_options.produce_model = true;
  const auto model_only_result = primo::smt::check_sat_smtlib(R"(
    (set-logic QF_LRA)
    (declare-fun x () Real)
    (declare-fun p () Bool)
    (assert p)
    (check-sat)
  )",
                                                              model_options);
  check(model_only_result.result == primo::smt::CheckSatResult::Sat);
  check(model_only_result.statistics.get(":tactic.solve-qf-lra-model-only.invocations") == 1);
  check(model_only_result.model.find("define-fun x") != std::string::npos);
}

// The route policy (lia_settings_policy.hpp) is the only source of LIA
// engine defaults, and an explicit override always wins on every route.
void lia_settings_policy_resolves_route_defaults_and_overrides() {
  using primo::smt::LiaRoute;
  using primo::smt::LiaSettings;
  // QF_UFLIA runs the engine defaults; QF_LIA adds the four integrality
  // switches (docs/papers/mip-presolve-integrality.md).
  LiaSettings qf_lia_expected;
  qf_lia_expected.round_integer_bounds = true;
  qf_lia_expected.primitive_int_rows = true;
  qf_lia_expected.integer_slacks = true;
  qf_lia_expected.patch_integral_moves = true;
  check(primo::smt::default_lia_settings(LiaRoute::QfLia) == qf_lia_expected);
  check(primo::smt::default_lia_settings(LiaRoute::QfUflia) == LiaSettings{});

  for (const LiaRoute route : {LiaRoute::QfLia, LiaRoute::QfUflia}) {
    const LiaSettings defaults = primo::smt::default_lia_settings(route);
    check(primo::smt::resolve_lia_settings(route, {}) == defaults);

    // Every override wins, in the direction opposite to the route default.
    primo::smt::LiaSettingOverrides overrides;
    overrides.branch_limit = 7;
    overrides.gomory_cut_limit = 0;
    overrides.patch_limit = 3;
    overrides.conflict_generalization = !defaults.conflict_generalization;
    overrides.round_integer_bounds = !defaults.round_integer_bounds;
    overrides.primitive_int_rows = !defaults.primitive_int_rows;
    overrides.integer_slacks = !defaults.integer_slacks;
    overrides.patch_integral_moves = !defaults.patch_integral_moves;
    const LiaSettings resolved = primo::smt::resolve_lia_settings(route, overrides);
    check(resolved.branch_limit == 7);
    check(resolved.gomory_cut_limit == 0);
    check(resolved.patch_limit == 3);
    check(resolved.conflict_generalization == !defaults.conflict_generalization);
    check(resolved.round_integer_bounds == !defaults.round_integer_bounds);
    check(resolved.primitive_int_rows == !defaults.primitive_int_rows);
    check(resolved.integer_slacks == !defaults.integer_slacks);
    check(resolved.patch_integral_moves == !defaults.patch_integral_moves);

    // An override equal to the default is kept and changes nothing.
    primo::smt::LiaSettingOverrides same;
    same.round_integer_bounds = defaults.round_integer_bounds;
    same.conflict_generalization = defaults.conflict_generalization;
    check(primo::smt::resolve_lia_settings(route, same) == defaults);
  }

  // An explicit false turns a QF_LIA default off: it is not treated as unset.
  primo::smt::LiaSettingOverrides off;
  off.primitive_int_rows = false;
  const LiaSettings resolved = primo::smt::resolve_lia_settings(LiaRoute::QfLia, off);
  check(!resolved.primitive_int_rows);
  check(resolved.round_integer_bounds && resolved.integer_slacks && resolved.patch_integral_moves);
}

// Each LIA route's resolved settings are part of the dispatch leaf's
// identity, so two option sets that run different engine settings never
// share a fingerprint.
void lia_overrides_change_the_solver_strategy_identity() {
  primo::smt::Frontend frontend;
  primo::util::Statistics statistics;
  primo::smt::TacticContext context(frontend.terms(), statistics, {}, true);
  primo::smt::CheckSatOptions options;
  const primo::smt::Strategy base =
      primo::smt::make_session_solver_strategy(frontend, options, statistics, context);

  // The two routes' defaults differ, and the identity records both.
  check(base.canonical().find("(\"qf-lia.primitive-int-rows\" \"true\")") != std::string::npos);
  check(base.canonical().find("(\"qf-uflia.primitive-int-rows\" \"false\")") != std::string::npos);

  primo::smt::CheckSatOptions same;
  same.lia.conflict_generalization = true;
  check(
      primo::smt::make_session_solver_strategy(frontend, same, statistics, context).fingerprint() ==
      base.fingerprint());

  primo::smt::CheckSatOptions rows;
  rows.lia.primitive_int_rows = false;
  const primo::smt::Strategy without_rows =
      primo::smt::make_session_solver_strategy(frontend, rows, statistics, context);
  check(without_rows.fingerprint() != base.fingerprint());
  check(without_rows.canonical().find("(\"qf-lia.primitive-int-rows\" \"false\")") !=
        std::string::npos);
  check(without_rows.canonical().find("(\"qf-uflia.primitive-int-rows\" \"false\")") !=
        std::string::npos);
}
void lia_route_settings_reach_the_engine() {
  const auto input = [](std::string_view logic) {
    return "(set-logic " + std::string(logic) + R"()
      (declare-fun x () Int)
      (declare-fun y () Int)
      (assert (= (+ (* 2 x) (* 2 y)) 3))
      (check-sat)
    )";
  };
  // QF_LIA's default stores the row as x + y with integer thresholds, so the
  // fractional bound 3/2 is refuted during preparation, before patching.
  const auto qf_lia = primo::smt::check_sat_smtlib(input("QF_LIA"), {});
  check(qf_lia.result == primo::smt::CheckSatResult::Unsat);
  check(qf_lia.statistics.get(":lia.prep-fractional-int-thresholds") > 0);
  check(qf_lia.statistics.get(":lia.patch-sweeps") == 0);

  // QF_UFLIA's default leaves the switches off: the slack stays non-integer
  // and patching runs.
  const auto qf_uflia = primo::smt::check_sat_smtlib(input("QF_UFLIA"), {});
  check(qf_uflia.result == primo::smt::CheckSatResult::Unsat);
  check(qf_uflia.statistics.get(":lia.prep-fractional-int-thresholds") == 0);
  check(qf_uflia.statistics.get(":lia.patch-sweeps") > 0);

  // Overrides reach the QF_LIA engine: switches off brings patching back,
  // and a zero patch budget then disables it again.
  primo::smt::CheckSatOptions off;
  off.lia.round_integer_bounds = false;
  off.lia.primitive_int_rows = false;
  off.lia.integer_slacks = false;
  off.lia.patch_integral_moves = false;
  const auto patched = primo::smt::check_sat_smtlib(input("QF_LIA"), off);
  check(patched.result == primo::smt::CheckSatResult::Unsat);
  check(patched.statistics.get(":lia.prep-fractional-int-thresholds") == 0);
  check(patched.statistics.get(":lia.patch-sweeps") > 0);

  off.lia.patch_limit = 0;
  const auto unpatched = primo::smt::check_sat_smtlib(input("QF_LIA"), off);
  check(unpatched.result == primo::smt::CheckSatResult::Unsat);
  check(unpatched.statistics.get(":lia.patch-sweeps") == 0);
}

void production_solver_strategy_has_stable_typed_identity() {
  primo::smt::Frontend frontend;
  primo::util::Statistics statistics;
  primo::smt::TacticContext context(frontend.terms(), statistics, {}, true);
  primo::smt::CheckSatOptions options;
  options.mixed_dispatch = primo::smt::MixedDispatchMode::Wide;
  options.produce_model = true;

  const primo::smt::Strategy strategy =
      primo::smt::make_session_solver_strategy(frontend, options, statistics, context);
  check(strategy.kind() == primo::smt::StrategyKind::Leaf);
  check(strategy.leaf_id() == "solver-dispatch");
  // QF_LIA defaults the four integrality switches on, QF_UFLIA off.
  const auto lia = [](std::string_view integrality) {
    return "(\"branch-limit\" \"100000\") (\"conflict-generalization\" \"true\") "
           "(\"cube\" \"off\") (\"cube-steps\" \"1000\") (\"gomory-cut-limit\" \"100\") "
           "(\"integer-slacks\" \"" +
           std::string(integrality) + "\") (\"patch-integral-moves\" \"" +
           std::string(integrality) + "\") (\"patch-limit\" \"100\") (\"primitive-int-rows\" \"" +
           std::string(integrality) + "\") (\"round-integer-bounds\" \"" +
           std::string(integrality) + "\")";
  };
  const auto route = [](std::string_view prefix, std::string rendered) {
    for (std::size_t at = rendered.find("(\""); at != std::string::npos;
         at = rendered.find("(\"", at + 2)) {
      rendered.insert(at + 2, prefix);
    }
    return rendered;
  };
  check(strategy.canonical() ==
        "(leaf \"solver-dispatch\" (\"has-declared-real-constants\" \"false\") "
        "(\"logic\" \"\") (\"mixed-dispatch\" \"wide\") " +
            route("qf-lia.", lia("true")) + " " + route("qf-uflia.", lia("false")) + ")");
  check(strategy ==
        primo::smt::make_session_solver_strategy(frontend, options, statistics, context));
  check(strategy.fingerprint() ==
        primo::smt::make_session_solver_strategy(frontend, options, statistics, context)
            .fingerprint());

  primo::smt::TacticContext no_model_context(frontend.terms(), statistics, {}, false);
  options.produce_model = false;
  const primo::smt::Strategy no_model_strategy =
      primo::smt::make_session_solver_strategy(frontend, options, statistics, no_model_context);
  check(no_model_strategy == strategy);
  check(no_model_strategy.fingerprint() == strategy.fingerprint());

  options.experimental_split_bools = 3;
  const primo::smt::Strategy split_strategy =
      primo::smt::make_session_solver_strategy(frontend, options, statistics, no_model_context);
  check(split_strategy.kind() == primo::smt::StrategyKind::Sequence);
  check(split_strategy.children().size() == 2);
  check(split_strategy.children()[0].leaf_id() == "experimental-split-bools");
  check(split_strategy.children()[0].parameters() ==
        std::vector<primo::smt::StrategyParameter>{{"k", "3"}});
  check(split_strategy.children()[1].leaf_id() == "solver-dispatch");
  check(split_strategy.canonical().find("(leaf \"experimental-split-bools\" (\"k\" \"3\"))") !=
        std::string::npos);
}

void session_portfolio_uses_typed_reasons_and_isolated_sequential_arms() {
  primo::util::Statistics first_statistics;
  first_statistics.set(primo::util::StatKey::TheoryConflicts, 7);
  RecordingSessionExecutor executor;
  executor.outcomes = {
      primo::smt::CheckSatOutcome{primo::smt::CheckSatResult::Unknown, "opaque diagnostic",
                                  first_statistics, "",
                                  primo::smt::InconclusiveReason::ResourceLimit},
      primo::smt::CheckSatOutcome{primo::smt::CheckSatResult::Sat, "", {}, "model"}};
  primo::smt::CheckSatOptions options;
  options.qf_uf_portfolio = primo::smt::QfUfPortfolioMode::BasicPolarity;
  options.qf_uf_portfolio_conflicts = 11;
  options.preprocessing_mode = primo::smt::PreprocessingMode::Strong;
  options.theory_phase = primo::smt::TheoryPhaseMode::Positive;

  const primo::smt::CheckSatOutcome result =
      primo::smt::execute_qf_uf_session_portfolio("(check-sat)", options, executor);
  check(result.result == primo::smt::CheckSatResult::Sat);
  check(result.model == "model");
  check(executor.options.size() == 2);
  check(executor.options[0].qf_uf_portfolio == primo::smt::QfUfPortfolioMode::Off);
  check(executor.options[0].preprocessing_mode == primo::smt::PreprocessingMode::Basic);
  check(executor.options[0].theory_phase == primo::smt::TheoryPhaseMode::Polarity);
  check(executor.options[0].max_theory_conflicts == 11);
  check(executor.options[1].qf_uf_portfolio == primo::smt::QfUfPortfolioMode::Off);
  check(executor.options[1].preprocessing_mode == primo::smt::PreprocessingMode::Strong);
  check(executor.options[1].theory_phase == primo::smt::TheoryPhaseMode::Positive);
  check(result.statistics.get(":portfolio.qf-uf.attempts") == 2);
  check(result.statistics.get(":portfolio.qf-uf.basic-polarity.first-conflicts") == 7);

  RecordingSessionExecutor ordinary_unknown;
  ordinary_unknown.outcomes = {
      primo::smt::CheckSatOutcome{primo::smt::CheckSatResult::Unknown,
                                  "theory conflict limit reached",
                                  {},
                                  "",
                                  primo::smt::InconclusiveReason::Unknown}};
  const primo::smt::CheckSatOutcome unknown =
      primo::smt::execute_qf_uf_session_portfolio("", options, ordinary_unknown);
  check(unknown.result == primo::smt::CheckSatResult::Unknown);
  check(ordinary_unknown.options.size() == 1);

  RecordingSessionExecutor cancelled;
  cancelled.outcomes = {primo::smt::CheckSatOutcome{primo::smt::CheckSatResult::Unknown,
                                                    "cancelled",
                                                    {},
                                                    "",
                                                    primo::smt::InconclusiveReason::Cancelled}};
  const primo::smt::CheckSatOutcome cancelled_result =
      primo::smt::execute_qf_uf_session_portfolio("", options, cancelled);
  check(cancelled_result.reason == primo::smt::InconclusiveReason::Cancelled);
  check(cancelled.options.size() == 1);

  RecordingSessionExecutor throwing;
  throwing.throw_on_execute = true;
  bool exception_escaped = false;
  try {
    static_cast<void>(primo::smt::execute_qf_uf_session_portfolio("", options, throwing));
  } catch (const std::runtime_error&) {
    exception_escaped = true;
  }
  check(exception_escaped);
}

}  // namespace

// C-004: a custom facet makes locale coverage independent of installed locales.
class ProbeTestPunctuation final : public std::numpunct<char> {
 protected:
  char do_decimal_point() const override { return ','; }
  char do_thousands_sep() const override { return '_'; }
  std::string do_grouping() const override { return "\3"; }
};

void probe_threshold_identity_is_round_trip_and_locale_independent() {
  using primo::smt::assertion_count;
  using primo::smt::at_most;
  Fixture fixture;
  const primo::smt::Goal empty;
  check(at_most(assertion_count(), 0.0)(fixture.context, empty));
  check(
      at_most(assertion_count(), std::numeric_limits<double>::infinity())(fixture.context, empty));
  check(!at_most(assertion_count(), -std::numeric_limits<double>::infinity())(fixture.context,
                                                                              empty));
  check(!at_most(assertion_count(), std::numeric_limits<double>::quiet_NaN())(fixture.context,
                                                                              empty));
  const double base = 1.0;
  const double next = std::nextafter(base, 2.0);
  check(at_most(assertion_count(), base).name() != at_most(assertion_count(), next).name());
  check(at_most(assertion_count(), 0.0).name() == at_most(assertion_count(), -0.0).name());
  check(at_most(assertion_count(), std::numeric_limits<double>::infinity()).name() ==
        "(<= assertion-count inf)");
  check(at_most(assertion_count(), -std::numeric_limits<double>::infinity()).name() ==
        "(<= assertion-count -inf)");
  check(at_most(assertion_count(), -std::numeric_limits<double>::quiet_NaN()).name() ==
        "(<= assertion-count nan)");
  for (const double value :
       {std::numeric_limits<double>::denorm_min(), std::numeric_limits<double>::min(),
        12345.678901234567, std::numeric_limits<double>::max()}) {
    const auto make = [value]() {
      return primo::smt::Strategy::conditional(at_most(assertion_count(), value),
                                               primo::smt::Strategy::skip(),
                                               primo::smt::Strategy::skip());
    };
    const auto normal = make();
    const std::locale previous = std::locale();
    std::locale::global(std::locale(std::locale::classic(), new ProbeTestPunctuation));
    const auto localized = make();
    check(normal.canonical() == localized.canonical());
    check(normal.fingerprint() == localized.fingerprint());
    std::locale::global(previous);
    check(at_most(assertion_count(), value).name() !=
          at_most(assertion_count(), std::nextafter(value, 0.0)).name());
  }
}

int main() {
  probe_threshold_identity_is_round_trip_and_locale_independent();
  incremental_context_rejects_unclassified_tactics();
  goal_truth_is_derived_from_assertions();
  reconstruction_records_are_goal_local_immutable_values();
  goal_updates_share_untouched_large_payloads();
  conversion_chain_replays_typed_steps_in_reverse_execution_order();
  anyof_has_disjunctive_semantics();
  empty_anyof_is_rejected();
  non_boolean_goal_assertion_is_a_contract_error();
  sequence_and_repeat_stop_at_a_fixed_point();
  repeat_preserves_equivalent_output_and_observes_terminal_changes();
  conditional_uses_a_probe();
  strategy_is_immutable_canonical_and_fingerprinted();
  strategy_probe_observes_the_transformed_goal_and_repeat_is_bounded();
  try_for_uses_context_deadlines_without_mutating_the_strategy();
  solver_leaf_handles_disjunctive_goals_in_order();
  experimental_split_bools_materializes_deterministic_goal_product();
  experimental_split_bools_prioritizes_symbols_and_bounds_materialization();
  experimental_split_bools_uses_sequential_first_sat_execution_and_keeps_model();
  sequential_scheduler_tries_each_arm_from_the_original_goal();
  cancellation_stops_before_running_a_tactic();
  deeply_nested_strategies_canonicalize_without_recursion();
  nested_strategies_compile_to_a_working_pipeline();
  triage_keeps_a_satisfiable_alternative_when_the_first_is_false();
  triage_reports_unsat_only_when_every_alternative_is_false();
  triage_flags_any_inconclusive_alternative();
  triage_passes_a_single_alternative_through();
  cancellation_during_a_repeat_terminates_it_promptly();
  cancellation_before_a_repeat_runs_nothing_inside_it();
  check_sat_honors_an_existing_cancellation_request();
  check_sat_preserves_cancellation_observed_by_preprocessing();
  frontend_type_errors_are_reported_as_unsupported();
  solver_dispatch_gives_cancellation_precedence_across_goals();
  solver_adapter_types_expected_inconclusive_results();
  typed_inconclusive_reason_overrides_terminal_formula_truth();
  preprocessing_fixed_point_and_terminal_change_statistics_are_exact();
  solver_adapter_does_not_hide_serious_failures();
  preprocessing_round_transforms_each_alternative_independently();
  finite_domain_snapshot_is_bound_to_the_executing_goal();
  symmetry_snapshot_is_bound_to_the_executing_goal();
  public_preprocessing_leaves_are_independently_constructible_and_typed();
  production_preprocessing_profile_and_strategy_are_explicit_and_stable();
  solver_dispatch_uses_formula_tactics();
  production_solver_strategy_has_stable_typed_identity();
  lia_settings_policy_resolves_route_defaults_and_overrides();
  lia_overrides_change_the_solver_strategy_identity();
  lia_route_settings_reach_the_engine();
  session_portfolio_uses_typed_reasons_and_isolated_sequential_arms();
}
