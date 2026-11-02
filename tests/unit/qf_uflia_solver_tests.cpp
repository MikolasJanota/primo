#ifdef NDEBUG
#undef NDEBUG
#endif

#include <array>
#include <cassert>
#include <string>
#include <vector>

#include "core/model.hpp"
#include "smt/check_sat.hpp"
#include "util/statistics.hpp"

using primo::smt::check_sat_smtlib;
using primo::smt::CheckSatOptions;
using primo::smt::CheckSatResult;

namespace {

// Expand the printed definitions in a fresh frontend, then check the ground
// negation. This checks the model's meaning without pinning arbitrary values
// or reusing the model builder. The same inputs also receive external Dolmen
// validation in the reconstruction regression gate.
void check_printed_model(const std::string& declarations,
                         const std::vector<std::string>& constraints,
                         primo::smt::PreprocessingMode mode, bool model_combination = true) {
  std::string input = "(set-logic QF_UFLIA)\n" + declarations;
  std::string conjunction = "(and";
  for (const auto& constraint : constraints) {
    input += "(assert " + constraint + ")\n";
    conjunction += " " + constraint;
  }
  conjunction += ")";
  CheckSatOptions options;
  options.produce_model = true;
  options.check_model = true;
  options.qf_uflia_model_combination = model_combination;
  options.preprocessing_mode = mode;
  const auto result = check_sat_smtlib(input + "(check-sat)\n", options);
  assert(result.result == CheckSatResult::Sat);
  assert(result.model.starts_with("(model"));
  assert(result.model.back() == ')');
  const std::string definitions = result.model.substr(6, result.model.size() - 7);
  const auto checked = check_sat_smtlib("(set-logic QF_UFLIA)\n" + definitions + "\n(assert (not " +
                                        conjunction + "))\n(check-sat)\n");
  assert(checked.result == CheckSatResult::Unsat);
}

void evaluated_argument_tuples_share_one_interpretation() {
  for (const auto mode :
       {primo::smt::PreprocessingMode::Off, primo::smt::PreprocessingMode::Basic,
        primo::smt::PreprocessingMode::Strong, primo::smt::PreprocessingMode::Auto}) {
    for (const bool combination : {false, true}) {
      for (const bool reversed : {false, true}) {
        const std::string declarations =
            "(declare-fun x () Int)\n(declare-fun y () Int)\n(declare-fun f (Int) Int)\n";
        check_printed_model(declarations,
                            reversed
                                ? std::vector<std::string>{"(= x 2)", "(= y (+ (f (+ x 1)) 1))"}
                                : std::vector<std::string>{"(= y (+ (f (+ x 1)) 1))", "(= x 2)"},
                            mode, combination);
        check_printed_model(declarations, {"(= x (f x))", "(= y (+ (f x) 1))"}, mode, combination);
      }
    }
  }
}

void eliminated_integer_definitions_are_evaluated_in_the_printed_model() {
  for (const auto mode :
       {primo::smt::PreprocessingMode::Off, primo::smt::PreprocessingMode::Basic,
        primo::smt::PreprocessingMode::Strong, primo::smt::PreprocessingMode::Auto}) {
    check_printed_model("(declare-fun z () Int)\n(declare-fun f (Int) Int)\n",
                        {"(= z (+ (f 0) (f 0)))"}, mode);
    check_printed_model(
        "(declare-fun x () Int)\n(declare-fun y () Int)\n(declare-fun z () Int)\n"
        "(declare-fun f (Int) Int)\n",
        {"(= x (+ (f 0) (f 0)))", "(= y (+ x 1))", "(= z (+ y (f x)))"}, mode);
    // Lowering the ite and then substituting z eliminates its auxiliary to x.
    // Recovery must see that saved equality before it looks up f(auxiliary).
    check_printed_model(
        "(declare-fun x () Int)\n(declare-fun w () Int)\n(declare-fun z () Int)\n"
        "(declare-fun f (Int) Int)\n",
        {"(= w (+ (f (ite (< z 1) x (- 1))) (f (ite (< z 1) x (- 1)))))", "(<= w 3)",
         "(= z (- 2))"},
        mode);
  }
}

void eliminated_integer_definition_chains_have_no_round_limit() {
  std::string declarations = "(declare-fun f (Int) Int)\n";
  std::vector<std::string> constraints;
  for (int index = 0; index < 32; ++index) {
    const std::string name = "v" + std::to_string(index);
    declarations += "(declare-fun " + name + " () Int)\n";
    constraints.push_back("(= " + name + " (+ " +
                          (index == 31 ? "(f 0)" : "v" + std::to_string(index + 1)) + " 1))");
  }
  check_printed_model(declarations, constraints, primo::smt::PreprocessingMode::Auto);
}

void accepts_integer_uninterpreted_functions() {
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_UFLIA)
    (declare-fun x () Int)
    (declare-fun f (Int) Int)
    (assert (= (f x) (+ x 1)))
    (check-sat)
  )");
  assert(result.result == CheckSatResult::Sat);
  assert(result.statistics.get(":solver.qf-uflia") == 1);
  assert(result.statistics.get(":combination.components") == 2);
  assert(result.statistics.contains(":time.qf-uflia.sat-us"));
  assert(result.statistics.contains(":time.qf-uflia.assignment-extraction-us"));
  assert(result.statistics.contains(":time.qf-uflia.euf-check-us"));
  assert(result.statistics.contains(":time.qf-uflia.lia-check-us"));
  assert(result.statistics.contains(":time.qf-uflia.lia-rational-us"));
  assert(result.statistics.contains(":time.qf-uflia.lia-integer-us"));
}

void strong_preprocessing_recognizes_integer_numeral_domains() {
  CheckSatOptions options;
  options.preprocessing_mode = primo::smt::PreprocessingMode::Strong;
  options.produce_model = true;
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_UFLIA)
    (declare-fun x () Int)
    (declare-fun y () Int)
    (declare-fun f (Int) Int)
    (assert (or (= x 1) (= x 2)))
    (assert (or (= y 1) (= y 2)))
    (assert (or (= x y) (= (f x) (f y))))
    (check-sat)
  )",
                                       options);
  assert(result.result == CheckSatResult::Sat);
  assert(result.statistics.get(":preprocessing.finite-domain.integer-numeral-choices") == 2);
  assert(result.statistics.get(":preprocessing.finite-domain.explicit-disequality-choices") == 0);
  assert(result.statistics.get(":preprocessing.boolean-nnf.visited") >= 1);
  assert(result.statistics.get(":preprocessing.equality-definitions") >= 1);
  assert(result.model.find("(define-fun x () Int") != std::string::npos);
  assert(result.model.find("(define-fun y () Int") != std::string::npos);
}

void auto_preprocessing_uses_the_evaluated_integer_safe_profile() {
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_UFLIA)
    (declare-fun x () Int)
    (declare-fun p () Bool)
    (declare-fun f (Int) Int)
    (assert (not (=> p (= (f x) 0))))
    (check-sat)
  )");
  assert(result.result == CheckSatResult::Sat);
  assert(result.statistics.get(":preprocessing.boolean-nnf.visited") >= 1);
  assert(result.statistics.get(":tactic.preprocessing-round.invocations") >= 1);

  CheckSatOptions off_options;
  off_options.preprocessing_mode = primo::smt::PreprocessingMode::Off;
  const auto off_result = check_sat_smtlib(R"(
    (set-logic QF_UFLIA)
    (declare-fun x () Int)
    (declare-fun p () Bool)
    (declare-fun f (Int) Int)
    (assert (not (=> p (= (f x) 0))))
    (check-sat)
  )",
                                           off_options);
  assert(off_result.result == CheckSatResult::Sat);
  assert(off_result.statistics.get(":preprocessing.boolean-nnf.visited") == 0);
  assert(off_result.statistics.get(":tactic.preprocessing-round.invocations") == 0);
}

// LIA is non-convex: 0 <= x <= 1 entails x=0 or x=1, but neither equality
// alone.  EUF derives both disequalities by congruence from the two function
// result disequalities.  Complete SAT-visible arrangement search must therefore
// reject both branches rather than wait for one individually entailed equality.
void non_convex_integer_arrangement_is_complete() {
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_UFLIA)
    (declare-fun x () Int)
    (declare-fun y () Int)
    (declare-fun z () Int)
    (declare-fun f (Int) Int)
    (assert (>= x 0))
    (assert (<= x 1))
    (assert (= y 0))
    (assert (= z 1))
    (assert (not (= (f x) (f y))))
    (assert (not (= (f x) (f z))))
    (check-sat)
  )");
  assert(result.result == CheckSatResult::Unsat);
  assert(result.statistics.get(":combination.arrangement-pairs") >= 3);
  assert(result.statistics.get(":combination.model.enabled") == 1);
  assert(result.statistics.get(":combination.online-components.enabled") == 1);
  assert(result.statistics.get(":combination.model.decisions") >= 1);
}

void online_components_preserve_non_convex_completeness() {
  CheckSatOptions options;
  options.qf_uflia_model_combination = false;
  options.qf_uflia_online_components = true;
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_UFLIA)
    (declare-fun x () Int)
    (declare-fun y () Int)
    (declare-fun z () Int)
    (declare-fun f (Int) Int)
    (assert (>= x 0))
    (assert (<= x 1))
    (assert (= y 0))
    (assert (= z 1))
    (assert (not (= (f x) (f y))))
    (assert (not (= (f x) (f z))))
    (check-sat)
  )",
                                       options);
  assert(result.result == CheckSatResult::Unsat);
  assert(result.statistics.get(":combination.online-components.enabled") == 1);
  assert(result.statistics.get(":euf.theory-conflicts") >= 1);
  assert(result.statistics.get(":combination.component-checks") == 0);
}

void online_components_build_a_combined_integer_uf_model() {
  CheckSatOptions options;
  options.qf_uflia_model_combination = false;
  options.qf_uflia_online_components = true;
  const auto result = check_sat_smtlib(R"(
    (set-option :produce-models true)
    (set-logic QF_UFLIA)
    (declare-fun x () Int)
    (declare-fun f (Int) Int)
    (assert (= x 2))
    (assert (= (f x) 3))
    (check-sat)
    (get-model)
  )",
                                       options);
  assert(result.result == CheckSatResult::Sat);
  assert(result.statistics.get(":combination.online-components.enabled") == 1);
  assert(result.statistics.get(":combination.online-components.integer-completions") == 1);
  // The generic final-check lifecycle asks both components. EUF confirms its
  // already-checked online state; only arithmetic performs integer completion.
  assert(result.statistics.get(":combination.component-checks") == 2);
  assert(result.model.find("(define-fun x () Int 2)") != std::string::npos);
  assert(result.model.find("(= x0 2) 3") != std::string::npos);
}

void model_guidance_handles_a_non_convex_disjunction_with_reversible_choices() {
  CheckSatOptions options;
  options.qf_uflia_model_combination = true;
  options.qf_uflia_online_components = false;
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_UFLIA)
    (declare-fun x () Int)
    (declare-fun y () Int)
    (declare-fun z () Int)
    (declare-fun f (Int) Int)
    (assert (>= x 0))
    (assert (<= x 1))
    (assert (= y 0))
    (assert (= z 1))
    (assert (not (= (f x) (f y))))
    (assert (not (= (f x) (f z))))
    (check-sat)
  )",
                                       options);
  assert(result.result == CheckSatResult::Unsat);
  assert(result.statistics.get(":combination.model.enabled") == 1);
  assert(result.statistics.get(":combination.model.decisions") >= 1);
  assert(result.statistics.get(":no.interface.model-equality-blocking-clauses") >= 1);
  assert(result.statistics.get(":combination.model.materialized-pairs") <
         result.statistics.get(":combination.model.total-pairs"));
}

void model_guidance_has_a_complete_arrangement_fallback() {
  CheckSatOptions options;
  options.qf_uflia_model_combination = true;
  options.qf_uflia_online_components = false;
  options.qf_uflia_model_decision_budget = 0;
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_UFLIA)
    (declare-fun x () Int)
    (declare-fun y () Int)
    (declare-fun f (Int) Int)
    (assert (<= x y))
    (assert (not (= (f x) (f y))))
    (check-sat)
  )",
                                       options);
  assert(result.result == CheckSatResult::Sat);
  assert(result.statistics.get(":combination.model.complete-fallbacks") == 1);
  assert(result.statistics.get(":combination.model.fallback-budget") == 1);
  assert(result.statistics.get(":combination.model.fallback-unsupported") == 0);
  assert(result.statistics.get(":combination.model.fallback-unknown") == 0);
  assert(result.statistics.get(":combination.model.fallback-resource-limit") == 0);
  assert(result.statistics.contains(":time.qf-uflia.complete-fallback-us"));
  assert(result.statistics.get(":combination.model.materialized-pairs") ==
         result.statistics.get(":combination.model.total-pairs"));
}

void agreeing_component_models_avoid_the_complete_arrangement() {
  CheckSatOptions options;
  options.qf_uflia_model_combination = true;
  options.qf_uflia_online_components = false;
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_UFLIA)
    (declare-fun x () Int)
    (declare-fun f (Int) Int)
    (assert (= (f x) (+ x 1)))
    (check-sat)
  )",
                                       options);
  assert(result.result == CheckSatResult::Sat);
  assert(result.statistics.get(":combination.model.total-pairs") == 1);
  assert(result.statistics.get(":combination.model.materialized-pairs") == 0);
  assert(result.statistics.get(":combination.model.snapshots") == 2);
}

void sparse_online_guidance_grows_components_and_rebuilds_online_state() {
  CheckSatOptions options;
  options.qf_uflia_model_combination = true;
  options.qf_uflia_online_components = true;
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_UFLIA)
    (declare-fun x () Int)
    (declare-fun y () Int)
    (declare-fun z () Int)
    (declare-fun f (Int) Int)
    (assert (>= x 0))
    (assert (<= x 1))
    (assert (= y 0))
    (assert (= z 1))
    (assert (not (= (f x) (f y))))
    (assert (not (= (f x) (f z))))
    (check-sat)
  )",
                                       options);
  assert(result.result == CheckSatResult::Unsat);
  assert(result.statistics.get(":combination.online-components.enabled") == 1);
  assert(result.statistics.get(":combination.online-components.rebuilds") >= 2);
  assert(result.statistics.get(":combination.components.full-builds") == 1);
  assert(result.statistics.get(":combination.components.incremental-growths") >= 1);
  assert(result.statistics.get(":combination.components.incremental-euf-atoms") ==
         result.statistics.get(":combination.components.incremental-growths"));
  assert(result.statistics.get(":combination.components.incremental-lia-atoms") >= 1);
  assert(result.statistics.get(":combination.components.incremental-lia-atoms") <=
         2 * result.statistics.get(":combination.components.incremental-growths"));
  assert(result.statistics.get(":combination.model.decisions") >= 1);
  assert(result.statistics.get(":no.interface.euf-care-pairs") >= 1);
  assert(result.statistics.get(":combination.model.care-pairs") >= 1);
}

void model_uses_the_arithmetic_value_of_a_purified_uf_argument() {
  CheckSatOptions options;
  options.qf_uflia_model_combination = true;
  const auto result = check_sat_smtlib(R"(
    (set-option :produce-models true)
    (set-logic QF_UFLIA)
    (declare-sort S 0)
    (declare-fun a () S)
    (declare-fun b () S)
    (declare-fun x () Int)
    (declare-fun f (Int) S)
    (assert (not (= a b)))
    (assert (< x 1))
    (assert (= (f (+ (* 4 x) 1)) a))
    (check-sat)
    (get-model)
  )",
                                       options);
  assert(result.result == CheckSatResult::Sat);
  assert(result.typed_model);
  const auto& model = *result.typed_model;
  const auto x = model.apply(2);
  const std::array argument{primo::core::ModelValue{
      x.sort, primo::util::Rational(4) * std::get<primo::util::Rational>(x.payload) +
                  primo::util::Rational(1)}};
  assert(model.apply(3, argument) == model.apply(0));
  assert(model.apply(0) != model.apply(1));
}

void model_combines_integer_values_and_uf_table() {
  const auto result = check_sat_smtlib(R"(
    (set-option :produce-models true)
    (set-logic QF_UFLIA)
    (declare-fun x () Int)
    (declare-fun f (Int) Int)
    (assert (= x 2))
    (assert (= (f x) 3))
    (check-sat)
    (get-model)
  )");
  assert(result.result == CheckSatResult::Sat);
  assert(result.model.find("(define-fun x () Int 2)") != std::string::npos);
  assert(result.model.find("(define-fun f ((x0 Int)) Int") != std::string::npos);
  assert(result.model.find("(= x0 2) 3") != std::string::npos);
}

void model_reconstructs_an_integer_substituted_by_a_uf_application() {
  const auto result = check_sat_smtlib(R"(
    (set-option :produce-models true)
    (set-logic QF_UFLIA)
    (declare-fun x () Int)
    (declare-fun y () Int)
    (declare-fun f (Int) Int)
    (assert (= (f y) x))
    (assert (<= x 2))
    (assert (< 3 (+ x y)))
    (check-sat)
    (get-model)
  )");
  assert(result.result == CheckSatResult::Sat);
  // This test is paired with direct Dolmen validation in the QF_UFLIA fuzz
  // gate. The old reconstruction printed x outside its asserted upper bound.
  assert(result.model.find("(define-fun x () Int 0)") != std::string::npos ||
         result.model.find("(define-fun x () Int 1)") != std::string::npos ||
         result.model.find("(define-fun x () Int 2)") != std::string::npos ||
         result.model.find("(define-fun x () Int (-") != std::string::npos);
}

// Solved-constant elimination runs before mixed purification. The recorded
// definition therefore contains f(a), while the LIA tableau contains only the
// later proxy equated to f(a). Reconstruction must carry that proxy value
// through the final EUF class before evaluating the compound definition.
void model_reconstructs_a_compound_definition_with_a_uf_application() {
  const auto result = check_sat_smtlib(R"(
    (set-option :produce-models true)
    (set-logic QF_UFLIA)
    (declare-fun a () Int)
    (declare-fun x () Int)
    (declare-fun f (Int) Int)
    (assert (= a 2))
    (assert (= (f a) 2))
    (assert (= x (+ (- 7) (* 4 (f a)))))
    (assert (< x 2))
    (check-sat)
    (get-model)
  )");
  assert(result.result == CheckSatResult::Sat);
  assert(result.model.find("(define-fun x () Int 1)") != std::string::npos);
  assert(result.model.find("(= x0 2) 2") != std::string::npos);
}

void unused_integer_uf_gets_a_well_sorted_default() {
  const auto result = check_sat_smtlib(R"(
    (set-option :produce-models true)
    (set-logic QF_UFLIA)
    (declare-fun f (Int) Int)
    (check-sat)
    (get-model)
  )");
  assert(result.result == CheckSatResult::Sat);
  assert(result.model.find("(define-fun f ((x0 Int)) Int 0)") != std::string::npos);
}

// Regression for the legacy candidate-scan evaluator's cyclic dependencies.
// The typed builder now follows the term DAG and consults one tuple table.
void model_completion_terminates_on_nested_uf_applications() {
  CheckSatOptions options;
  options.produce_model = true;
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_UFLIA)
    (declare-fun f (Int) Int)
    (declare-fun x () Int)
    (assert (= x 1))
    (assert (>= (f (f x)) 0))
    (check-sat)
  )",
                                       options);
  assert(result.result == CheckSatResult::Sat);
  assert(result.model.find("(define-fun x () Int 1)") != std::string::npos);
}

void model_completion_terminates_on_deeply_nested_uf_applications() {
  std::string nested = "x";
  for (int depth = 0; depth < 200; ++depth) {
    nested = "(f " + nested + ")";
  }
  CheckSatOptions options;
  options.produce_model = true;
  const auto result = check_sat_smtlib(
      "(set-logic QF_UFLIA)\n"
      "(declare-fun f (Int) Int)\n"
      "(declare-fun x () Int)\n"
      "(assert (= x 1))\n"
      "(assert (>= " +
          nested +
          " 0))\n"
          "(check-sat)\n",
      options);
  assert(result.result == CheckSatResult::Sat);
}

// A constant eliminated before its auxiliary must be restored after that
// auxiliary, using the interpretation at that replay stage.
void substituted_constant_over_substituted_auxiliary_is_valued() {
  CheckSatOptions options;
  options.produce_model = true;
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_UFLIA)
    (declare-fun x () Int)
    (declare-fun y () Int)
    (declare-fun z () Int)
    (declare-fun f (Int) Int)
    (assert (= z (- 2)))
    (assert (= x (f (+ (ite (>= z 0) y (- 2)) (f y)))))
    (assert (<= x 2))
    (check-sat)
  )",
                                       options);
  assert(result.result == CheckSatResult::Sat);
  // x is neither an LIA variable nor a member of any EUF atom here, so its
  // value exists only if the substitution chain is followed all the way down.
  // Printing an invented value for it is what produced an invalid model.
  assert(result.model.find("(define-fun x () Int") != std::string::npos);
  assert(result.statistics.get(":model.invented-int-values") <= 6);
}

void invented_int_model_values_are_counted() {
  CheckSatOptions options;
  options.produce_model = true;
  options.check_model = true;

  const auto two_levels = check_sat_smtlib(R"(
    (set-logic QF_UFLIA)
    (declare-fun f (Int) Int)
    (declare-fun v1 () Int)
    (assert (<= v1 6))
    (assert (= v1 (f (+ (f 3) (- 1)))))
    (check-sat)
  )",
                                           options);
  assert(two_levels.result == CheckSatResult::Sat);
  assert(two_levels.statistics.get(":model.invented-int-values") == 0);

  // Historical witness: completion remains a diagnostic, not a validity
  // oracle. Explicit original-assertion checking validates the interpretation.
  const auto three_levels = check_sat_smtlib(R"(
    (set-logic QF_UFLIA)
    (declare-fun f (Int) Int)
    (declare-fun v1 () Int)
    (assert (<= v1 6))
    (assert (= v1 (f (f (+ (f 3) (- 1))))))
    (check-sat)
  )",
                                             options);
  assert(three_levels.result == CheckSatResult::Sat);
  assert(three_levels.statistics.contains(":model.invented-int-values"));
}

void model_parameters_do_not_shadow_user_symbols() {
  const auto result = check_sat_smtlib(R"(
    (set-option :produce-models true)
    (set-logic QF_UFLIA)
    (declare-fun x1 (Int Int) Int)
    (assert (= (x1 0 1) 2))
    (check-sat)
    (get-model)
  )");
  assert(result.result == CheckSatResult::Sat);
  assert(result.model.find("(define-fun x1 ((x0 Int) (@primo_model_arg_1 Int)) Int") !=
         std::string::npos);
  assert(result.model.find("(= @primo_model_arg_1 1)") != std::string::npos);
}

// The combined route prepares Int atoms through the same LIA adapter, so
// --lia-round-integer-bounds must reach it: (f x) in [4/3, 5/3] contains no
// integer, which the rounded bounds expose without integer completion.
void rounding_integer_bounds_reaches_the_combined_route() {
  const std::string script = R"(
    (set-logic QF_UFLIA)
    (declare-fun f (Int) Int)
    (declare-fun x () Int)
    (assert (<= (* 3 (f x)) 5))
    (assert (>= (* 3 (f x)) 4))
    (check-sat)
  )";
  CheckSatOptions rounded;
  rounded.lia_round_integer_bounds = true;
  const auto result = check_sat_smtlib(script, rounded);
  assert(result.result == CheckSatResult::Unsat);
  assert(result.statistics.get(primo::util::StatKey::LiaPatchSweeps) == 0);
  assert(check_sat_smtlib(script, CheckSatOptions{}).result == CheckSatResult::Unsat);
}

void rejects_real_symbols() {
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_UFLIA)
    (declare-fun f (Int) Real)
    (check-sat)
  )");
  assert(result.result == CheckSatResult::Unsupported);

  const auto real_argument = check_sat_smtlib(R"(
    (set-logic QF_UFLIA)
    (declare-fun f (Real) Int)
    (check-sat)
  )");
  assert(real_argument.result == CheckSatResult::Unsupported);
}

}  // namespace

int main() {
  evaluated_argument_tuples_share_one_interpretation();
  eliminated_integer_definitions_are_evaluated_in_the_printed_model();
  eliminated_integer_definition_chains_have_no_round_limit();
  accepts_integer_uninterpreted_functions();
  strong_preprocessing_recognizes_integer_numeral_domains();
  auto_preprocessing_uses_the_evaluated_integer_safe_profile();
  non_convex_integer_arrangement_is_complete();
  online_components_preserve_non_convex_completeness();
  online_components_build_a_combined_integer_uf_model();
  model_guidance_handles_a_non_convex_disjunction_with_reversible_choices();
  model_guidance_has_a_complete_arrangement_fallback();
  agreeing_component_models_avoid_the_complete_arrangement();
  sparse_online_guidance_grows_components_and_rebuilds_online_state();
  model_uses_the_arithmetic_value_of_a_purified_uf_argument();
  model_combines_integer_values_and_uf_table();
  model_reconstructs_an_integer_substituted_by_a_uf_application();
  model_reconstructs_a_compound_definition_with_a_uf_application();
  unused_integer_uf_gets_a_well_sorted_default();
  model_completion_terminates_on_nested_uf_applications();
  model_completion_terminates_on_deeply_nested_uf_applications();
  invented_int_model_values_are_counted();
  substituted_constant_over_substituted_auxiliary_is_valued();
  model_parameters_do_not_shadow_user_symbols();
  rounding_integer_bounds_reaches_the_combined_route();
  rejects_real_symbols();
  return 0;
}
