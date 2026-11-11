#ifdef NDEBUG
#undef NDEBUG
#endif

#include <cassert>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>

#include "smt/check_sat.hpp"
#include "smt/lia_settings_policy.hpp"
#include "theory/arithmetic/lia_cube_mode.hpp"
#include "util/statistics.hpp"

using primo::smt::CheckSatOptions;
using primo::smt::CheckSatResult;

namespace {

// The engine's own defaults (LiaSettings{}), with the integrality switches
// off. QF_LIA's route default turns them on (lia_settings_policy.cpp), so
// tests of the stages below pin these settings explicitly instead of
// inheriting the route default; the route default is tested separately.
CheckSatOptions engine_defaults() {
  CheckSatOptions options;
  options.lia.round_integer_bounds = false;
  options.lia.primitive_int_rows = false;
  options.lia.integer_slacks = false;
  options.lia.patch_integral_moves = false;
  return options;
}

const auto check_sat_smtlib = [](std::string_view input,
                                 CheckSatOptions options = engine_defaults()) {
  options.check_model = true;
  return primo::smt::check_sat_smtlib(input, std::move(options));
};

CheckSatOptions small_branch_limit(std::size_t limit) {
  CheckSatOptions options = engine_defaults();
  options.lia.branch_limit = limit;
  return options;
}

CheckSatOptions branch_and_cut_limits(std::size_t branch_limit, std::size_t cut_limit) {
  CheckSatOptions options = engine_defaults();
  options.lia.branch_limit = branch_limit;
  options.lia.gomory_cut_limit = cut_limit;
  return options;
}

void trivially_sat_needs_no_branching() {
  const auto result = check_sat_smtlib(R"(
    (set-option :produce-models true)
    (set-logic QF_LIA)
    (declare-fun x () Int)
    (assert (= x 7))
    (check-sat)
    (get-model)
  )");
  assert(result.result == CheckSatResult::Sat);
  assert(result.model.find("(define-fun x () Int 7)") != std::string::npos);
}

// A Boolean constant can disappear from the final SAT problem when preprocessing proves its only
// occurrence tautological. It still has to be assigned in the SMT-LIB model: Dolmen rejects partial
// models that omit declared constants.
void model_completes_preprocessed_boolean_constants() {
  const auto result = check_sat_smtlib(R"(
    (set-option :produce-models true)
    (set-logic QF_LIA)
    (declare-fun x () Int)
    (declare-fun p () Bool)
    (assert (= x 2))
    (assert (or p (not p)))
    (check-sat)
    (get-model)
  )");
  assert(result.result == CheckSatResult::Sat);
  assert(result.model.find("(define-fun x () Int 2)") != std::string::npos);
  assert(result.model.find("(define-fun p () Bool ") != std::string::npos);
}

// docs/open-leads.md lead 12a: bare-form solved-constant substitution
// (x=t, x not occurring in t) is enabled by default for QF_LIA. y=5 and
// x=y+3 chains across two of the substitution loop's passes -- the first
// pass accepts y=5 (no other pending candidate depends on it) and
// rewrites x=y+3 to x=5+3 in the same pass; the second pass then accepts
// x=5+3 directly. Both eliminated variables never reach the tableau at
// all (PreparedLraTheory::variables_by_term has no entry for either), so
// this exercises build_qf_lia_model's substitution-aware fallback
// (evaluate_qf_lia_model_term), not just the rewrite itself -- the bug
// this same lead found and fixed defaulted an eliminated variable's
// model value to 0 instead of resolving it.
void chained_solved_constant_substitution_reconstructs_the_model() {
  const auto result = check_sat_smtlib(R"(
    (set-option :produce-models true)
    (set-logic QF_LIA)
    (declare-fun x () Int)
    (declare-fun y () Int)
    (assert (= y 5))
    (assert (= x (+ y 3)))
    (check-sat)
    (get-model)
  )");
  assert(result.result == CheckSatResult::Sat);
  assert(result.model.find("(define-fun x () Int 8)") != std::string::npos);
  assert(result.model.find("(define-fun y () Int 5)") != std::string::npos);
}

void two_x_equals_three_is_unsat() {
  // 2x == 3 has no integer solution at all: both branches (x<=1, x>=2)
  // contradict the fixed bound 2x==3 immediately.
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_LIA)
    (declare-fun x () Int)
    (assert (= (* 2 x) 3))
    (check-sat)
  )");
  assert(result.result == CheckSatResult::Unsat);
}

void one_branch_finds_an_integer_solution() {
  // The LRA relaxation of 2y == x + 1 with 1<=x<=3 can land on a
  // fractional x; one branch-and-bound split resolves it (e.g. x=1,y=1).
  const auto result = check_sat_smtlib(R"(
    (set-option :produce-models true)
    (set-logic QF_LIA)
    (declare-fun x () Int)
    (declare-fun y () Int)
    (assert (>= x 1))
    (assert (<= x 3))
    (assert (= (* 2 y) (+ x 1)))
    (check-sat)
    (get-model)
  )");
  assert(result.result == CheckSatResult::Sat);
  const std::size_t x_pos = result.model.find("(define-fun x () Int ");
  const std::size_t y_pos = result.model.find("(define-fun y () Int ");
  assert(x_pos != std::string::npos);
  assert(y_pos != std::string::npos);
}

void nested_branching_finds_a_solution() {
  // 3x + 5y + 7z == 100 with generous bounds needs the search to resolve
  // more than one fractional variable before landing on an integer point.
  const auto result = check_sat_smtlib(R"(
    (set-option :produce-models true)
    (set-logic QF_LIA)
    (declare-fun x () Int)
    (declare-fun y () Int)
    (declare-fun z () Int)
    (assert (= (+ (* 3 x) (* 5 y) (* 7 z)) 100))
    (assert (>= x 0))
    (assert (>= y 0))
    (assert (>= z 0))
    (assert (<= x 15))
    (assert (<= y 15))
    (assert (<= z 15))
    (check-sat)
    (get-model)
  )");
  assert(result.result == CheckSatResult::Sat);
  assert(result.model.find("(define-fun x () Int ") != std::string::npos);
  assert(result.model.find("(define-fun y () Int ") != std::string::npos);
  assert(result.model.find("(define-fun z () Int ") != std::string::npos);
}

void distinct_over_tight_range_is_unsat() {
  // Pigeonhole: three pairwise-distinct Int constants squeezed into a
  // 2-value domain.
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_LIA)
    (declare-fun a () Int)
    (declare-fun b () Int)
    (declare-fun c () Int)
    (assert (>= a 1))
    (assert (<= a 2))
    (assert (>= b 1))
    (assert (<= b 2))
    (assert (>= c 1))
    (assert (<= c 2))
    (assert (distinct a b c))
    (check-sat)
  )");
  assert(result.result == CheckSatResult::Unsat);
}

void distinct_with_enough_room_is_sat() {
  const auto result = check_sat_smtlib(R"(
    (set-option :produce-models true)
    (set-logic QF_LIA)
    (declare-fun a () Int)
    (declare-fun b () Int)
    (declare-fun c () Int)
    (assert (>= a 1))
    (assert (<= a 3))
    (assert (>= b 1))
    (assert (<= b 3))
    (assert (>= c 1))
    (assert (<= c 3))
    (assert (distinct a b c))
    (check-sat)
    (get-model)
  )");
  assert(result.result == CheckSatResult::Sat);
}

// (< x 5) and (> x 3) over Int should tighten to x<=4 and x>=4, pinning
// x=4 exactly -- lra_atom.cpp's strict-inequality tightening, exercised
// end to end through the solver rather than white-box on LraAtom alone.
void strict_inequalities_tighten_to_pin_one_value() {
  const auto sat_result = check_sat_smtlib(R"(
    (set-option :produce-models true)
    (set-logic QF_LIA)
    (declare-fun x () Int)
    (assert (< x 5))
    (assert (> x 3))
    (check-sat)
    (get-model)
  )");
  assert(sat_result.result == CheckSatResult::Sat);
  assert(sat_result.model.find("(define-fun x () Int 4)") != std::string::npos);

  const auto unsat_result = check_sat_smtlib(R"(
    (set-logic QF_LIA)
    (declare-fun x () Int)
    (assert (< x 5))
    (assert (> x 3))
    (assert (not (= x 4)))
    (check-sat)
  )");
  assert(unsat_result.result == CheckSatResult::Unsat);
}

// 2x - 2y == 1 has no integer solution (the left side is always even).
// Stage 2's GCD-consistency check (docs/qf-lia-branch-and-bound.md)
// catches this directly from the tableau row, without ever invoking
// branch-and-bound -- so this must come back Unsat immediately even under
// a node limit far too small for branch-and-bound alone to ever reach
// Unsat on its own (see lia_branch_and_bound_tests.cpp's
// unbounded_polytope_hits_the_node_limit for that fact, confirmed at the
// LraTableau level).
void gcd_inconsistent_system_is_unsat_without_branching() {
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_LIA)
    (declare-fun x () Int)
    (declare-fun y () Int)
    (assert (= (* 2 x) (+ (* 2 y) 1)))
    (check-sat)
  )",
                                       small_branch_limit(1));
  assert(result.result == CheckSatResult::Unsat);
}

// 4x + 4y in [2,3], x and y unbounded: GCD-consistency does not fire here
// (the row is bounded on both sides, not fixed to a single value, and
// 2 and 3 are consecutive integers -- no single fixed residual to check
// divisibility against), yet no integer x, y satisfies it (4x+4y is
// always a multiple of 4, and no multiple of 4 lies in [2,3]). This is
// exactly the kind of instance stage 2 deliberately does NOT close: the
// LRA relaxation stays feasible at every branch-and-bound node (real x,y
// can pick 4x+4y anywhere in [2,3]), so the search never runs out of new
// fractional variables to split on and must report Unknown rather than
// hang, matching docs/qf-lia-branch-and-bound.md's documented remaining
// incompleteness after stage 2.
void gcd_uncatchable_divergent_system_hits_the_node_limit() {
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_LIA)
    (declare-fun x () Int)
    (declare-fun y () Int)
    (assert (>= (+ (* 4 x) (* 4 y)) 2))
    (assert (<= (+ (* 4 x) (* 4 y)) 3))
    (check-sat)
  )",
                                       small_branch_limit(20));
  assert(result.result == CheckSatResult::Unknown);
  assert(result.message.find("node limit") != std::string::npos);
}

// The QF_LIA route default enables the integrality switches
// (lia_settings_policy.cpp). With primitive rows, the previous test's row is
// stored as x + y with bounds [1/2, 3/4]; rounding them inward to [1, 0]
// refutes it before branch-and-bound, within the same node budget. Under
// set-logic QF_UFLIA the switches stay off and the node limit is reached.
void route_default_closes_the_scaled_interval_gap() {
  const std::string body = R"(
    (declare-fun x () Int)
    (declare-fun y () Int)
    (assert (>= (+ (* 4 x) (* 4 y)) 2))
    (assert (<= (+ (* 4 x) (* 4 y)) 3))
    (check-sat)
  )";
  CheckSatOptions route_default;
  route_default.lia.branch_limit = 20;
  route_default.check_model = true;
  const auto qf_lia = primo::smt::check_sat_smtlib("(set-logic QF_LIA)" + body, route_default);
  assert(qf_lia.result == CheckSatResult::Unsat);
  assert(qf_lia.statistics.get(":lia.prep-fractional-int-thresholds") > 0);
  const auto qf_uflia = primo::smt::check_sat_smtlib("(set-logic QF_UFLIA)" + body, route_default);
  assert(qf_uflia.result == CheckSatResult::Unknown);
}

// 3x + 5y = 7, x,y >= 0: no non-negative integer solution exists (y=0
// needs x=7/3; y=1 needs 3x=2; y>=2 makes 5y>7 alone), confirmed
// independently against z3 (unsat). The LP relaxation is feasible
// (e.g. x=7/3, y=0), so branch-and-bound alone needs to actually branch
// to discover the infeasibility -- with a branch limit of 1 it cannot,
// and reports Unknown (stage 1's own honest incompleteness). Stage 3's
// Gomory cut pre-check resolves this from the very first candidate model
// without branch-and-bound needing to explore anything, demonstrating
// cuts genuinely extend what stage 1+2 alone could resolve within the
// same node budget -- the concrete, user-visible point of this stage.
void gomory_cut_resolves_what_branch_and_bound_alone_cannot() {
  const char* const problem = R"(
    (set-logic QF_LIA)
    (declare-fun x () Int)
    (declare-fun y () Int)
    (assert (= (+ (* 3 x) (* 5 y)) 7))
    (assert (>= x 0))
    (assert (>= y 0))
    (check-sat)
  )";

  const auto without_cuts = check_sat_smtlib(problem, branch_and_cut_limits(1, 0));
  assert(without_cuts.result == CheckSatResult::Unknown);
  assert(without_cuts.message.find("node limit") != std::string::npos);

  const auto with_cuts = check_sat_smtlib(problem, branch_and_cut_limits(1, 100));
  assert(with_cuts.result == CheckSatResult::Unsat);
}

// docs/qf-lia-branch-and-bound.md's "Conflict generalization" section.
// `3x + 5y = 7, x,y >= 0` is the same instance the Gomory-cut section uses:
// unsat, LRA-feasible, and closed by a cut rather than by branching. Here it
// pins the *lemma* the cut produces rather than the answer.
//
// The z and w constraints are deliberately irrelevant to the contradiction:
// they are satisfiable on their own and share no variable with it, so they
// are exactly the kind of atom the coarse clause names and the derivation
// does not. Without them the instance is too small to distinguish the two
// forms -- all four of its atoms genuinely participate in the cut, so the
// derived lemma is the same size as the coarse one and the test would prove
// nothing. With them, the coarse clause is 8 literals and the derivation is
// still 4.
void conflict_generalization_narrows_the_lemma_to_the_derivation() {
  const std::string script = R"(
    (set-logic QF_LIA)
    (declare-fun x () Int)
    (declare-fun y () Int)
    (declare-fun z () Int)
    (declare-fun w () Int)
    (assert (= (+ (* 3 x) (* 5 y)) 7))
    (assert (>= x 0))
    (assert (>= y 0))
    (assert (>= z 0))
    (assert (<= z 10))
    (assert (>= w 5))
    (assert (<= w 20))
    (check-sat)
  )";
  const auto result = check_sat_smtlib(script, engine_defaults());
  assert(result.result == CheckSatResult::Unsat);

  const auto generalized = result.statistics.get(primo::util::StatKey::LiaGeneralizedLemmas);
  const auto fallbacks = result.statistics.get(primo::util::StatKey::LiaGeneralizationFallbacks);
  const auto derived_lits =
      result.statistics.get(primo::util::StatKey::LiaGeneralizedLemmaLiterals);
  const auto coarse_lits = result.statistics.get(primo::util::StatKey::LiaCoarseLemmaLiterals);
  assert(generalized > 0);
  assert(fallbacks == 0);
  assert(derived_lits < coarse_lits);
}

// The switch has to be a pure quality knob: turning generalization off
// restores the coarse clause and must not change any answer. Asserted on
// both an unsat instance (where the lemma is what drives the outer loop) and
// a sat one (where no lemma is emitted at all), since a bad lemma shows up
// as either a wrong answer or a non-terminating loop.
void conflict_generalization_does_not_change_answers() {
  const std::string unsat_script = R"(
    (set-logic QF_LIA)
    (declare-fun x () Int)
    (declare-fun y () Int)
    (assert (= (+ (* 3 x) (* 5 y)) 7))
    (assert (>= x 0))
    (assert (>= y 0))
    (check-sat)
  )";
  const std::string sat_script = R"(
    (set-logic QF_LIA)
    (declare-fun x () Int)
    (declare-fun y () Int)
    (assert (<= 3 (+ x y)))
    (assert (<= (+ x y) 4))
    (assert (distinct x y))
    (check-sat)
  )";
  CheckSatOptions coarse = engine_defaults();
  coarse.lia.conflict_generalization = false;
  const CheckSatOptions generalized = engine_defaults();

  assert(check_sat_smtlib(unsat_script, generalized).result == CheckSatResult::Unsat);
  assert(check_sat_smtlib(unsat_script, coarse).result == CheckSatResult::Unsat);
  assert(check_sat_smtlib(sat_script, generalized).result == CheckSatResult::Sat);
  assert(check_sat_smtlib(sat_script, coarse).result == CheckSatResult::Sat);

  // With generalization off, every lemma is the coarse one, by definition.
  const auto coarse_result = check_sat_smtlib(unsat_script, coarse);
  assert(coarse_result.statistics.get(primo::util::StatKey::LiaGeneralizedLemmas) == 0);
}

// docs/qf-lia-branch-and-bound.md's "Patching" section. 2y = x with
// 3 <= x <= 10: the LRA relaxation puts x at its bound 3, leaving y = 3/2.
// Patching moves x to 4 instead, making y = 2 with no search at all.
//
// A branch limit of *zero* is what makes this discriminating rather than
// merely passing: branch_and_bound cannot take a single node, so it can only
// return LimitReached on a fractional assignment. A Sat answer therefore
// proves patching integralized everything on its own. (Zero is settable
// programmatically but not on the CLI, where --lia-branch-limit starts at 1.)
void patching_finds_a_model_without_branching() {
  const std::string script = R"(
    (set-option :produce-models true)
    (set-logic QF_LIA)
    (declare-fun x () Int)
    (declare-fun y () Int)
    (assert (>= x 3))
    (assert (<= x 10))
    (assert (= (* 2 y) x))
    (check-sat)
    (get-model)
  )";
  CheckSatOptions patched = engine_defaults();
  patched.lia.branch_limit = 0;
  const auto result = check_sat_smtlib(script, patched);
  assert(result.result == CheckSatResult::Sat);
  assert(result.statistics.get(primo::util::StatKey::LiaPatchedVariables) > 0);
  assert(result.model.find("(define-fun x () Int 4)") != std::string::npos);
  assert(result.model.find("(define-fun y () Int 2)") != std::string::npos);

  // The same instance with patching off cannot be solved at that branch
  // limit, which is what pins the gain to patching specifically.
  CheckSatOptions unpatched = engine_defaults();
  unpatched.lia.branch_limit = 0;
  unpatched.lia.patch_limit = 0;
  assert(check_sat_smtlib(script, unpatched).result == CheckSatResult::Unknown);
}

// Prevalence counters for the integer-bound gaps (docs/implementation/lia.md).
// `3*x <= 5` leaves the fractional threshold 5/3 on Int x; `2*y + 3*z` and
// `4*x + 2*y` normalize to slacks with fractional coefficients, which
// bound three atoms in total.
void integer_preparation_counters_report_bound_gaps() {
  const std::string script = R"(
    (set-logic QF_LIA)
    (declare-fun x () Int)
    (declare-fun y () Int)
    (declare-fun z () Int)
    (assert (<= (* 3 x) 5))
    (assert (>= x 1))
    (assert (<= (+ (* 2 y) (* 3 z)) 7))
    (assert (>= (+ (* 2 y) (* 3 z)) 1))
    (assert (< (+ (* 2 y) (* 4 x)) 9))
    (check-sat)
  )";
  const auto result = check_sat_smtlib(script, engine_defaults());
  assert(result.result == CheckSatResult::Sat);
  assert(result.statistics.get(primo::util::StatKey::LiaPrepFractionalIntThresholds) == 1);
  assert(result.statistics.get(primo::util::StatKey::LiaPrepNonIntegerIntSlackAtoms) == 3);
  assert(result.statistics.get(primo::util::StatKey::LiaPrepNonIntegerIntSlacks) == 2);
}

// --lia-round-integer-bounds (King thesis Section 4.3): `3*x <= 5` becomes
// x <= 1, so an instance that is feasible over the reals but not over the
// integers is refuted by the rational check alone, and a satisfiable one gets
// an integral assignment without patching. Without the option the answers
// must be the same; only the integer-completion work differs.
void rounding_integer_bounds_keeps_answers_and_skips_completion() {
  const auto run = [](const std::string& body, bool round) {
    CheckSatOptions options = engine_defaults();
    options.lia.round_integer_bounds = round;
    options.produce_model = true;
    return check_sat_smtlib("(set-logic QF_LIA)\n(declare-fun x () Int)\n(declare-fun y () Int)\n" +
                                body + "(check-sat)\n",
                            options);
  };
  // Upper thresholds 5/3 on both variables; their sum cannot reach 3.
  const std::string upper = "(assert (<= (* 3 x) 5))(assert (<= (* 3 y) 5))(assert (>= (+ x y) 3))";
  // A negated strict atom and a negative scale both give lower thresholds 5/3.
  const std::string negated_lt = "(assert (not (< (* 3 x) 5)))(assert (<= x 1))";
  const std::string negative_scale = "(assert (<= (* (- 3) x) (- 5)))(assert (<= x 1))";
  for (const std::string& body : {upper, negated_lt, negative_scale}) {
    const auto rounded = run(body, true);
    assert(rounded.result == CheckSatResult::Unsat);
    assert(rounded.statistics.get(primo::util::StatKey::LiaPatchSweeps) == 0);
    assert(run(body, false).result == CheckSatResult::Unsat);
  }

  // x in [1/2, 5/3]: rounding leaves exactly x = 1, already integral.
  const std::string sat_body = "(assert (<= (* 3 x) 5))(assert (>= (* 2 x) 1))(assert (= y x))";
  const auto rounded = run(sat_body, true);
  assert(rounded.result == CheckSatResult::Sat);
  assert(rounded.model.find("(define-fun x () Int 1)") != std::string::npos);
  assert(rounded.statistics.get(primo::util::StatKey::LiaPatchedVariables) == 0);
  assert(run(sat_body, false).result == CheckSatResult::Sat);
}

// --lia-primitive-int-rows (King thesis Section 4.3): Int rows keep coprime
// integer coefficients, so their slacks are integer-valued and, together
// with --lia-round-integer-bounds, get integral bounds. Every case must give
// the same answer under all four combinations of the two options.
void primitive_integer_rows_keep_answers() {
  const auto run = [](const std::string& body, bool round, bool primitive) {
    CheckSatOptions options = engine_defaults();
    options.lia.round_integer_bounds = round;
    options.lia.primitive_int_rows = primitive;
    options.produce_model = true;
    return check_sat_smtlib("(set-logic QF_LIA)\n(declare-fun x () Int)\n(declare-fun y () Int)\n" +
                                body + "(check-sat)\n",
                            options);
  };
  struct Case {
    std::string body;
    CheckSatResult expected;
  };
  const std::vector<Case> cases{
      // 2x + 4y = 3 has no integer solution (parity); s = x + 2y in [3/2, 3/2].
      {"(assert (>= (+ (* 2 x) (* 4 y)) 3))(assert (<= (+ (* 2 x) (* 4 y)) 3))",
       CheckSatResult::Unsat},
      // A strict compound atom: 2x + 3y < 1 is 2x + 3y <= 0 over the integers.
      {"(assert (< (+ (* 2 x) (* 3 y)) 1))(assert (>= (+ (* 2 x) (* 3 y)) 0))(assert (>= x 0))",
       CheckSatResult::Sat},
      // A large leading coefficient: under leading-one normalization the
      // slack is x + y/2^32, which must not be rounded as an integer.
      {"(assert (<= (+ (* 4294967296 x) y) (- 5)))(assert (>= y 0))(assert (<= y 10))",
       CheckSatResult::Sat},
      // Proportional atoms with opposite signs share one row: 2x + 3y lies in
      // [13/2, 7]. With unbounded x and y, branch-and-bound alone can exhaust
      // its node limit (unknown); the integral slack bound s = 7 lets one
      // patch move find x = 2, y = 1.
      {"(assert (<= (+ (* 2 x) (* 3 y)) 7))(assert (<= (+ (* (- 4) x) (* (- 6) y)) (- 13)))",
       CheckSatResult::Sat},
  };
  for (const Case& test_case : cases) {
    for (const bool round : {false, true}) {
      for (const bool primitive : {false, true}) {
        const CheckSatResult result = run(test_case.body, round, primitive).result;
        // No combination may contradict the expected answer; the full
        // integrality preparation must also decide every case.
        assert(result == test_case.expected ||
               (result == CheckSatResult::Unknown && !(round && primitive)));
      }
    }
  }
  const auto proportional = run(cases[3].body, true, true);
  assert(proportional.statistics.get(primo::util::StatKey::LiaPatchedVariables) > 0);

  // The parity case needs no integer completion once both options hold:
  // the rounded slack bounds 2 <= s <= 1 conflict in the rational check.
  const auto both = run(cases[0].body, true, true);
  assert(both.statistics.get(primo::util::StatKey::LiaPatchSweeps) == 0);
  assert(both.statistics.get(primo::util::StatKey::LiaPrepNonIntegerIntSlackAtoms) == 0);
  const auto rounded_only = run(cases[0].body, true, false);
  assert(rounded_only.statistics.get(primo::util::StatKey::LiaPatchSweeps) > 0);
  assert(rounded_only.statistics.get(primo::util::StatKey::LiaPrepNonIntegerIntSlackAtoms) == 2);

  // A satisfiable large-coefficient instance still gets an exact model.
  const auto model =
      run("(assert (= (+ (* 4294967296 x) y) (- 4294967291)))(assert (>= y 0))"
          "(assert (<= y 10))",
          true, true);
  assert(model.result == CheckSatResult::Sat);
  assert(model.model.find("(define-fun x () Int (- 1))") != std::string::npos);
  assert(model.model.find("(define-fun y () Int 5)") != std::string::npos);
}

// --lia-integer-slacks makes completion branch on integer slacks. With
// integer rows but no bound rounding, s = 2x + 3y lies in [13/2, 27/4]: one
// split on s refutes it, while splitting only x and y (unbounded) cannot
// within a small node budget.
void integer_slacks_join_completion() {
  const std::string script = R"(
    (set-logic QF_LIA)
    (declare-fun x () Int)
    (declare-fun y () Int)
    (assert (<= (+ (* 8 x) (* 12 y)) 27))
    (assert (>= (+ (* 4 x) (* 6 y)) 13))
    (check-sat)
  )";
  CheckSatOptions options = engine_defaults();
  options.lia.primitive_int_rows = true;
  options.lia.branch_limit = 50;
  const auto inputs_only = check_sat_smtlib(script, options);
  assert(inputs_only.result == CheckSatResult::Unknown);
  assert(inputs_only.statistics.get(primo::util::StatKey::LiaPrepIntegerSlacks) == 1);
  options.lia.integer_slacks = true;
  const auto with_slacks = check_sat_smtlib(script, options);
  assert(with_slacks.result == CheckSatResult::Unsat);
}

// --lia-patch-integral-moves reaches the patcher through LiaCompletionLimits:
// its counters move only when the option is set, and the answer is the same.
// The integrality options together must not contradict any answer either.
void integral_patch_moves_reach_completion() {
  const std::string script = R"(
    (set-logic QF_LIA)
    (declare-fun x () Int)
    (declare-fun y () Int)
    (declare-fun z () Int)
    (assert (<= (* 3 x) 5))
    (assert (>= x 1))
    (assert (<= (+ (* 2 y) (* 3 z)) 7))
    (assert (>= (+ (* 2 y) (* 3 z)) 1))
    (assert (< (+ (* 2 y) (* 4 x)) 9))
    (check-sat)
  )";
  CheckSatOptions plain = engine_defaults();
  const auto off = check_sat_smtlib(script, plain);
  assert(off.result == CheckSatResult::Sat);
  assert(off.statistics.get(primo::util::StatKey::LiaPatchFractionalCandidatesRejected) == 0);
  assert(off.statistics.get(primo::util::StatKey::LiaPatchLatticeCandidatesScored) == 0);
  CheckSatOptions integral = engine_defaults();
  integral.lia.patch_integral_moves = true;
  const auto on = check_sat_smtlib(script, integral);
  assert(on.result == CheckSatResult::Sat);
  assert(on.statistics.get(primo::util::StatKey::LiaPatchFractionalCandidatesRejected) > 0);
  assert(on.statistics.get(primo::util::StatKey::LiaPatchLatticeCandidatesScored) > 0);
  assert(on.statistics.get(primo::util::StatKey::LiaPatchFractionalNonbasicMoves) == 0);

  CheckSatOptions all = integral;
  all.lia.round_integer_bounds = true;
  all.lia.primitive_int_rows = true;
  all.lia.integer_slacks = true;
  const std::string header = "(set-logic QF_LIA)\n(declare-fun x () Int)\n(declare-fun y () Int)\n";
  const std::vector<std::pair<std::string, CheckSatResult>> cases{
      {"(assert (>= (+ (* 2 x) (* 4 y)) 3))(assert (<= (+ (* 2 x) (* 4 y)) 3))",
       CheckSatResult::Unsat},
      {"(assert (< (+ (* 2 x) (* 3 y)) 1))(assert (>= (+ (* 2 x) (* 3 y)) 0))(assert (>= x 0))",
       CheckSatResult::Sat},
      {"(assert (<= (+ (* 4294967296 x) y) (- 5)))(assert (>= y 0))(assert (<= y 10))",
       CheckSatResult::Sat},
      {"(assert (<= (+ (* 2 x) (* 3 y)) 7))(assert (<= (+ (* (- 4) x) (* (- 6) y)) (- 13)))",
       CheckSatResult::Sat},
      {"(assert (<= (+ (* 8 x) (* 12 y)) 27))(assert (>= (+ (* 4 x) (* 6 y)) 13))",
       CheckSatResult::Unsat},
      {"(assert (<= (* 3 x) 5))(assert (<= (* 3 y) 5))(assert (>= (+ x y) 3))",
       CheckSatResult::Unsat},
  };
  for (const auto& [body, expected] : cases) {
    assert(check_sat_smtlib(header + body + "(check-sat)\n", all).result == expected);
  }
  assert(check_sat_smtlib(script, all).result == CheckSatResult::Sat);
}

// The integer preparation switches belong to the LIA adapter only: a QF_LRA
// problem must solve identically when they are set.
void integer_preparation_options_do_not_touch_real_arithmetic() {
  const std::string script = R"(
    (set-logic QF_LRA)
    (declare-fun x () Real)
    (declare-fun y () Real)
    (assert (<= (+ (* 2 x) (* 3 y)) 5))
    (assert (>= (+ (* 2 x) (* 3 y)) 5))
    (assert (>= x (/ 1 3)))
    (check-sat)
  )";
  CheckSatOptions plain = engine_defaults();
  plain.produce_model = true;
  CheckSatOptions integer = plain;
  integer.lia.round_integer_bounds = true;
  integer.lia.primitive_int_rows = true;
  const auto a = check_sat_smtlib(script, plain);
  const auto b = check_sat_smtlib(script, integer);
  assert(a.result == CheckSatResult::Sat && b.result == CheckSatResult::Sat);
  assert(a.model == b.model);
}

// The budget is the A/B control, so turning it off must change no answer --
// asserted on a sat instance (where patching does the work) and an unsat one
// (where it can only waste time). A patch that broke feasibility would show up
// here as a wrong answer rather than a slow one.
void patching_does_not_change_answers() {
  const std::string sat_script = R"(
    (set-logic QF_LIA)
    (declare-fun x () Int)
    (declare-fun y () Int)
    (assert (>= x 1))
    (assert (<= x 3))
    (assert (= (* 2 y) (+ x 1)))
    (check-sat)
  )";
  const std::string unsat_script = R"(
    (set-logic QF_LIA)
    (declare-fun x () Int)
    (declare-fun y () Int)
    (assert (= (+ (* 3 x) (* 5 y)) 7))
    (assert (>= x 0))
    (assert (>= y 0))
    (check-sat)
  )";
  CheckSatOptions without = engine_defaults();
  without.lia.patch_limit = 0;
  const CheckSatOptions with = engine_defaults();

  assert(check_sat_smtlib(sat_script, with).result == CheckSatResult::Sat);
  assert(check_sat_smtlib(sat_script, without).result == CheckSatResult::Sat);
  assert(check_sat_smtlib(unsat_script, with).result == CheckSatResult::Unsat);
  assert(check_sat_smtlib(unsat_script, without).result == CheckSatResult::Unsat);

  const auto disabled = check_sat_smtlib(sat_script, without);
  assert(disabled.statistics.get(primo::util::StatKey::LiaPatchSweeps) == 0);
}

// A patched model has to be a real model. The tightest end-to-end check
// available here is an instance whose solution is unique, so any move that
// broke feasibility would produce a visibly wrong value rather than merely a
// different one: (< x 5) and (> x 3) over Int pin x = 4 exactly.
void patched_model_satisfies_a_uniquely_determined_instance() {
  const auto result = check_sat_smtlib(R"(
    (set-option :produce-models true)
    (set-logic QF_LIA)
    (declare-fun x () Int)
    (declare-fun y () Int)
    (assert (< x 5))
    (assert (> x 3))
    (assert (= y (* 3 x)))
    (check-sat)
    (get-model)
  )");
  assert(result.result == CheckSatResult::Sat);
  assert(result.model.find("(define-fun x () Int 4)") != std::string::npos);
  assert(result.model.find("(define-fun y () Int 12)") != std::string::npos);
}

void qf_lia_rejects_uninterpreted_functions() {
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_LIA)
    (declare-fun f (Int) Int)
    (check-sat)
  )");
  assert(result.result == CheckSatResult::Unsupported);
  assert(result.message.find("uninterpreted functions") != std::string::npos);
}

void qf_lia_rejects_real_constants() {
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_LIA)
    (declare-fun x () Real)
    (check-sat)
  )");
  assert(result.result == CheckSatResult::Unsupported);
}

void qf_lia_rejects_division() {
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_LIA)
    (declare-fun x () Int)
    (assert (= (/ x 2) 3))
    (check-sat)
  )");
  assert(result.result == CheckSatResult::Unsupported);
}

void int_constants_require_qf_lia_logic() {
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_LRA)
    (declare-fun x () Int)
    (check-sat)
  )");
  assert(result.result == CheckSatResult::Unsupported);
}

// SMT-LIB fixes div and mod by Boute's Euclidean definition, so the remainder
// is non-negative whatever the signs are. The two negative-divisor rows are
// where that parts company with the floored definition, so they are pinned
// through the solver rather than only through the term constructors.
void qf_lia_follows_the_euclidean_division_convention() {
  struct Row {
    const char* numerator;
    const char* divisor;
    const char* quotient;
    const char* remainder;
  };
  const Row rows[] = {
      {"7", "3", "2", "1"},
      {"(- 7)", "3", "(- 3)", "2"},
      {"7", "(- 3)", "(- 2)", "1"},
      {"(- 7)", "(- 3)", "3", "2"},
  };
  for (const Row& row : rows) {
    const std::string program = std::string("(set-logic QF_LIA)\n(assert (= (div ") +
                                row.numerator + " " + row.divisor + ") " + row.quotient +
                                "))\n(assert (= (mod " + row.numerator + " " + row.divisor + ") " +
                                row.remainder + "))\n(check-sat)\n";
    assert(check_sat_smtlib(program).result == CheckSatResult::Sat);
  }
}

// The same convention through the encoding rather than through constant
// folding: the numerator is a variable, so a quotient constant is introduced.
void qf_lia_encodes_div_and_mod_over_a_variable() {
  const auto consistent = check_sat_smtlib(R"(
    (set-logic QF_LIA)
    (declare-fun x () Int)
    (assert (= x 10))
    (assert (= (div x 3) 3))
    (assert (= (mod x 3) 1))
    (check-sat)
  )");
  assert(consistent.result == CheckSatResult::Sat);

  // div and mod over the same numerator and divisor share one quotient, so an
  // inconsistent pair is unsat rather than satisfiable through two witnesses.
  const auto inconsistent = check_sat_smtlib(R"(
    (set-logic QF_LIA)
    (declare-fun x () Int)
    (assert (= x 10))
    (assert (= (mod x 3) 2))
    (check-sat)
  )");
  assert(inconsistent.result == CheckSatResult::Unsat);

  // The remainder is confined to [0, |d|) whatever the numerator is.
  const auto out_of_range = check_sat_smtlib(R"(
    (set-logic QF_LIA)
    (declare-fun x () Int)
    (assert (= (mod x 3) 3))
    (check-sat)
  )");
  assert(out_of_range.result == CheckSatResult::Unsat);

  const auto negative_remainder = check_sat_smtlib(R"(
    (set-logic QF_LIA)
    (declare-fun x () Int)
    (assert (< (mod x 5) 0))
    (check-sat)
  )");
  assert(negative_remainder.result == CheckSatResult::Unsat);
}

void qf_lia_supports_abs_and_unit_divisors() {
  const auto absolute = check_sat_smtlib(R"(
    (set-logic QF_LIA)
    (declare-fun x () Int)
    (assert (= x (- 4)))
    (assert (= (abs x) 4))
    (check-sat)
  )");
  assert(absolute.result == CheckSatResult::Sat);

  const auto never_negative = check_sat_smtlib(R"(
    (set-logic QF_LIA)
    (declare-fun x () Int)
    (assert (< (abs x) 0))
    (check-sat)
  )");
  assert(never_negative.result == CheckSatResult::Unsat);

  const auto unit = check_sat_smtlib(R"(
    (set-logic QF_LIA)
    (declare-fun x () Int)
    (assert (= x 6))
    (assert (= (div x 1) 6))
    (assert (= (mod x 1) 0))
    (assert (= (div x (- 1)) (- 6)))
    (check-sat)
  )");
  assert(unit.result == CheckSatResult::Sat);
}

// A non-constant divisor is nonlinear, not unknown, and the message has to say
// so: reporting "unknown function symbol" is what made this look like a
// missing feature rather than a fragment boundary.
void qf_lia_rejects_nonlinear_and_zero_division() {
  const auto nonlinear = check_sat_smtlib(R"(
    (set-logic QF_LIA)
    (declare-fun x () Int)
    (declare-fun y () Int)
    (assert (= (div x y) 1))
    (check-sat)
  )");
  assert(nonlinear.result == CheckSatResult::Unsupported);
  assert(nonlinear.message.find("nonlinear") != std::string::npos);

  const auto by_zero = check_sat_smtlib(R"(
    (set-logic QF_LIA)
    (declare-fun x () Int)
    (assert (= (div x 0) 1))
    (check-sat)
  )");
  assert(by_zero.result == CheckSatResult::Unsupported);
  assert(by_zero.message.find("division by zero") != std::string::npos);

  // abs belongs to the Ints theory only.
  const auto real_abs = check_sat_smtlib(R"(
    (set-logic QF_LRA)
    (declare-fun r () Real)
    (assert (>= (abs r) 0))
    (check-sat)
  )");
  assert(real_abs.result == CheckSatResult::Unsupported);
}

// Lowering runs in the initial sequence, before the preprocessing mode is
// consulted, so no setting can leave an extended term for the solver.
void qf_lia_lowers_extended_terms_with_preprocessing_off() {
  CheckSatOptions options = engine_defaults();
  options.preprocessing_mode = primo::smt::PreprocessingMode::Off;
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_LIA)
    (declare-fun x () Int)
    (assert (= x 10))
    (assert (= (+ (div x 3) (mod x 3)) 4))
    (assert (= (abs (- x)) 10))
    (check-sat)
  )",
                                       options);
  assert(result.result == CheckSatResult::Sat);
}

// One arithmetic component must retain SAT-owned Boolean values without EUF,
// regardless of unrelated combination options or EUF scheduling.
void shared_driver_preserves_boolean_models_and_recipe_isolation() {
  for (const bool guided : {false, true}) {
    for (const auto euf : {primo::smt::EufEngine::Lazy, primo::smt::EufEngine::Incremental}) {
      CheckSatOptions options = engine_defaults();
      options.preprocessing_mode = primo::smt::PreprocessingMode::Off;
      options.qf_uflia_model_combination = guided;
      options.qf_uflia_online_components = false;
      options.qf_uflia_model_decision_budget = 0;
      options.euf_engine = euf;
      const auto result = check_sat_smtlib(R"(
        (set-logic QF_LIA)
        (set-option :produce-models true)
        (declare-const x Int)
        (declare-const unused Int)
        (declare-const p Bool)
        (declare-const q Bool)
        (declare-const unused_bool Bool)
        (assert (= (* 2 x) 4))
        (assert (or p (> x 3)))
        (assert (not q))
        (check-sat)
        (get-model)
      )",
                                           options);
      assert(result.result == CheckSatResult::Sat);
      assert(result.model.find("(define-fun p () Bool true)") != std::string::npos);
      assert(result.model.find("(define-fun q () Bool false)") != std::string::npos);
      assert(result.model.find("(define-fun unused () Int") != std::string::npos);
      assert(result.model.find("(define-fun unused_bool () Bool") != std::string::npos);
      assert(result.statistics.get(":combination.components") == 1);
      assert(result.statistics.get(":combination.online-components.enabled") == 1);
      assert(result.statistics.get(":combination.model.materialized-pairs") == 0);
      assert(result.statistics.get(":combination.arrangement-bridge-clauses") == 0);
      assert(result.statistics.get(":combination.model.complete-fallbacks") == 0);
      assert(!result.statistics.contains(":time.qf-uflia.lia-check-us"));
    }
  }
}

void boolean_only_lia_and_solving_without_models() {
  CheckSatOptions options = engine_defaults();
  options.preprocessing_mode = primo::smt::PreprocessingMode::Off;
  const auto boolean = check_sat_smtlib(R"(
    (set-logic QF_LIA)
    (set-option :produce-models true)
    (declare-const p Bool)
    (declare-const q Bool)
    (assert p)
    (assert (xor p q))
    (check-sat)
    (get-model)
  )",
                                        options);
  assert(boolean.result == CheckSatResult::Sat);
  assert(boolean.model.find("(define-fun p () Bool true)") != std::string::npos);
  assert(boolean.model.find("(define-fun q () Bool false)") != std::string::npos);
  assert(boolean.statistics.get(":combination.components") == 1);
  options.produce_model = false;
  const auto arithmetic = primo::smt::check_sat_smtlib(R"(
    (set-logic QF_LIA)
    (declare-const x Int)
    (assert (> (* 3 x) 1))
    (assert (< (* 3 x) 5))
    (check-sat)
  )",
                                                       options);
  assert(arithmetic.result == CheckSatResult::Sat);
  assert(arithmetic.model.empty());
  assert(arithmetic.typed_model == nullptr);
}

void internal_integer_branches_do_not_spend_the_conflict_budget() {
  constexpr std::string_view input = R"(
    (set-logic QF_LIA)
    (declare-const x Int)
    (declare-const y Int)
    (assert (= (+ (* 2 x) (* 3 y)) 5))
    (assert (>= x 0))
    (assert (>= y 0))
    (assert (<= x 3))
    (assert (<= y 3))
    (check-sat)
  )";
  CheckSatOptions options = engine_defaults();
  options.preprocessing_mode = primo::smt::PreprocessingMode::Off;
  options.lia.patch_limit = 0;
  options.lia.gomory_cut_limit = 0;
  options.lia.branch_limit = 0;
  options.max_theory_conflicts = 1;
  const auto no_search = check_sat_smtlib(input, options);
  assert(no_search.result == CheckSatResult::Unknown);
  assert(no_search.reason == primo::smt::InconclusiveReason::ResourceLimit);
  assert(no_search.statistics.get(primo::util::StatKey::TheoryConflictCount) == 0);
  options.lia.branch_limit = 100;
  const auto searched = check_sat_smtlib(input, options);
  assert(searched.result == CheckSatResult::Sat);
  assert(searched.statistics.get(primo::util::StatKey::TheoryConflictCount) == 0);
}

void shared_driver_preserves_typed_limits_and_cancellation() {
  constexpr std::string_view input = R"(
    (set-logic QF_LIA)
    (declare-const x Int)
    (declare-const y Int)
    (assert (= (+ (* 2 x) (* 4 y)) 1))
    (check-sat)
  )";
  CheckSatOptions options = engine_defaults();
  options.preprocessing_mode = primo::smt::PreprocessingMode::Off;
  options.max_theory_conflicts = 1;
  const auto limited = check_sat_smtlib(input, options);
  assert(limited.result == CheckSatResult::Unknown);
  assert(limited.reason == primo::smt::InconclusiveReason::ResourceLimit);
  assert(limited.statistics.get(primo::util::StatKey::TheoryConflictCount) == 1);
  options.max_theory_conflicts.reset();
  const auto complete = check_sat_smtlib(input, options);
  assert(complete.result == CheckSatResult::Unsat);
  assert(complete.statistics.get(primo::util::StatKey::TheoryConflictCount) == 1);
  options.max_theory_conflicts = 0;
  const auto zero = check_sat_smtlib(input, options);
  assert(zero.result == CheckSatResult::Unknown);
  assert(zero.reason == primo::smt::InconclusiveReason::ResourceLimit);
  assert(zero.statistics.get(primo::util::StatKey::TheoryConflictCount) == 0);
  options.max_theory_conflicts.reset();
  options.cancelled = [] { return true; };
  const auto cancelled = check_sat_smtlib(input, options);
  assert(cancelled.result == CheckSatResult::Unknown);
  assert(cancelled.reason == primo::smt::InconclusiveReason::Cancelled);
}

void cube_controls_and_model_reconstruction() {
  using primo::theory::arithmetic::LiaCubeMode;
  primo::smt::LiaSettingOverrides requested;
  requested.cube_mode = LiaCubeMode::Difference;
  assert(primo::smt::resolve_lia_settings(primo::smt::LiaRoute::QfUflia, requested).cube_mode ==
         LiaCubeMode::Off);
  const std::string script = R"(
    (set-logic QF_LIA)
    (declare-const x Int)
    (declare-const y Int)
    (assert (<= (+ (* 3 x) y) 9))
    (assert (not (<= (+ (* 3 x) y) 2)))
    (check-sat)
  )";
  for (const auto mode : {LiaCubeMode::Uniform, LiaCubeMode::Difference}) {
    auto options = engine_defaults();
    options.preprocessing_mode = primo::smt::PreprocessingMode::Off;
    options.lia.patch_limit = 0;
    options.lia.gomory_cut_limit = 0;
    options.lia.cube_mode = mode;
    options.check_model = true;
    options.produce_model = true;
    const auto outcome = check_sat_smtlib(script, options);
    assert(outcome.result == CheckSatResult::Sat);
    assert(outcome.statistics.get(primo::util::StatKey::LiaCubeModels) > 0);
    assert(!outcome.model.empty());
    options.lia.cube_steps = 0;
    options.lia.branch_limit = 100;
    const auto budgeted = check_sat_smtlib(script, options);
    options.lia.cube_mode = LiaCubeMode::Off;
    const auto baseline = check_sat_smtlib(script, options);
    assert(budgeted.result == baseline.result && budgeted.reason == baseline.reason);
    assert(budgeted.statistics.get(primo::util::StatKey::LiaBranchNodes) ==
           baseline.statistics.get(primo::util::StatKey::LiaBranchNodes));
    assert(budgeted.statistics.get(primo::util::StatKey::LiaCubeLimits) > 0);
  }
}

}  // namespace

int main() {
  {
    auto options = engine_defaults();
    options.preprocessing_mode = primo::smt::PreprocessingMode::Off;
    options.lia.hybrid_branching = true;
    options.lia.private_branch_factor = 0;
    options.lia.patch_limit = 0;
    options.lia.gomory_cut_limit = 0;
    const auto lower = check_sat_smtlib(R"(
      (set-logic QF_LIA) (declare-const x Int)
      (assert (<= (* 2 x) (- 1))) (check-sat)
    )",
                                        options);
    assert(lower.result == CheckSatResult::Sat);
    assert(lower.statistics.get(":lia.branching.unique-atoms") == 1);
    assert(lower.statistics.get(":lia.branching.choice-conflicts") == 0);
    // Force repeated atom and term-arena growth with independently fractional
    // coordinates. ASan covers these allocations in the full suite.
    std::string input = "(set-logic QF_LIA)\n";
    for (int i = 0; i < 40; ++i) {
      const auto name = "x" + std::to_string(i);
      input += "(declare-const " + name + " Int)(assert (<= 1 (* 2 " + name + ")))\n";
    }
    const auto grown = check_sat_smtlib(input + "(check-sat)", options);
    assert(grown.result == CheckSatResult::Sat);
    assert(grown.statistics.get(":lia.branching.unique-atoms") == 40);
    assert(grown.statistics.get(":lia.branching.existing-coordinate-registrations") == 40);
  }
  {
    CheckSatOptions options;
    options.preprocessing_mode = primo::smt::PreprocessingMode::Off;
    options.lia.hybrid_branching = true;
    options.lia.private_branch_factor = 0;
    options.lia.patch_limit = 0;
    options.lia.gomory_cut_limit = 0;
    const std::string formula = R"(
      (set-logic QF_LIA)
      (declare-const x Int) (declare-const y Int)
      (assert (<= 1 (* 2 x))) (assert (<= (* 2 x) 5))
      (assert (<= 1 (* 2 y))) (assert (<= (* 2 y) 5))
      (check-sat)
    )";
    // Disable bound rounding so both rational half-integers reach completion.
    options.lia.round_integer_bounds = false;
    const auto result = check_sat_smtlib(formula, options);
    assert(result.result == CheckSatResult::Sat);
    assert(result.statistics.get(":lia.branching.unique-atoms") >= 2);
    assert(result.statistics.get(":lia.branching.choice-conflicts") >= 1);
    options.lia.split_atom_limit = 1;
    const auto limited = check_sat_smtlib(formula, options);
    assert(limited.result == CheckSatResult::Unknown);
    assert(limited.statistics.get(":lia.branching.split-budget-exhaustions") == 1);
  }
  cube_controls_and_model_reconstruction();
  shared_driver_preserves_boolean_models_and_recipe_isolation();
  boolean_only_lia_and_solving_without_models();
  shared_driver_preserves_typed_limits_and_cancellation();
  internal_integer_branches_do_not_spend_the_conflict_budget();
  qf_lia_follows_the_euclidean_division_convention();
  qf_lia_encodes_div_and_mod_over_a_variable();
  qf_lia_supports_abs_and_unit_divisors();
  qf_lia_rejects_nonlinear_and_zero_division();
  qf_lia_lowers_extended_terms_with_preprocessing_off();
  trivially_sat_needs_no_branching();
  model_completes_preprocessed_boolean_constants();
  chained_solved_constant_substitution_reconstructs_the_model();
  two_x_equals_three_is_unsat();
  one_branch_finds_an_integer_solution();
  nested_branching_finds_a_solution();
  distinct_over_tight_range_is_unsat();
  distinct_with_enough_room_is_sat();
  strict_inequalities_tighten_to_pin_one_value();
  gcd_inconsistent_system_is_unsat_without_branching();
  gcd_uncatchable_divergent_system_hits_the_node_limit();
  route_default_closes_the_scaled_interval_gap();
  gomory_cut_resolves_what_branch_and_bound_alone_cannot();
  conflict_generalization_narrows_the_lemma_to_the_derivation();
  conflict_generalization_does_not_change_answers();
  patching_finds_a_model_without_branching();
  patching_does_not_change_answers();
  integer_preparation_counters_report_bound_gaps();
  rounding_integer_bounds_keeps_answers_and_skips_completion();
  primitive_integer_rows_keep_answers();
  integer_slacks_join_completion();
  integral_patch_moves_reach_completion();
  integer_preparation_options_do_not_touch_real_arithmetic();
  patched_model_satisfies_a_uniquely_determined_instance();
  qf_lia_rejects_uninterpreted_functions();
  qf_lia_rejects_real_constants();
  qf_lia_rejects_division();
  int_constants_require_qf_lia_logic();
  return 0;
}
