#ifdef NDEBUG
#undef NDEBUG
#endif

#include <array>
#include <cassert>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

#include "core/model.hpp"
#include "smt/check_sat.hpp"
#include "smt/inconclusive_reason.hpp"
#include "smt/preprocessing.hpp"

using primo::smt::CheckSatOptions;
using primo::smt::CheckSatResult;

namespace {

const auto check_sat_smtlib = [](std::string_view input, CheckSatOptions options = {}) {
  options.check_model = true;
  return primo::smt::check_sat_smtlib(input, std::move(options));
};

std::string read_text_file(const char* path) {
  std::ifstream input(path);
  assert(input.good());
  std::ostringstream buffer;
  buffer << input.rdbuf();
  return buffer.str();
}

primo::smt::CheckSatOptions propagation_test_options() {
  primo::smt::CheckSatOptions options;
  options.preprocessing_mode = primo::smt::PreprocessingMode::Off;
  options.uf_symmetry_breaking = false;
  options.eq_bridge = false;
  options.euf_propagation_interval = 1;
  options.euf_propagation_assign_threshold = 1.0;
  return options;
}

void pure_boolean_sat() {
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_UF)
    (declare-fun p () Bool)
    (declare-fun q () Bool)
    (assert (or p q))
    (check-sat)
  )");
  assert(result.result == CheckSatResult::Sat);
  assert(result.statistics.get(":input.assertions") == 1);
  assert(result.statistics.get(":preprocessing.passes") == 1);
  assert(result.statistics.get(":solver.boolean") == 1);
  assert(result.statistics.get(":solver.qf-uf") == 0);
  assert(result.statistics.get(":sat.solve-calls") == 1);
}

void get_model_requires_produce_models() {
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_UF)
    (check-sat)
    (get-model)
  )");
  assert(result.result == CheckSatResult::Unsupported);
  assert(result.message.find(":produce-models") != std::string::npos);
  assert(result.model.empty());
}

void get_model_requires_prior_check_sat() {
  const auto result = check_sat_smtlib(R"(
    (set-option :produce-models true)
    (set-logic QF_UF)
    (get-model)
    (check-sat)
  )");
  assert(result.result == CheckSatResult::Unsupported);
  assert(result.message.find("before check-sat") != std::string::npos);
  assert(result.model.empty());
}

void get_model_prints_qf_uf_model_for_declared_symbols() {
  const auto result = check_sat_smtlib(R"(
    (set-option :produce-models true)
    (set-logic QF_UF)
    (declare-sort A 0)
    (declare-fun a () A)
    (declare-fun b () A)
    (declare-fun f (A) A)
    (declare-fun p (A) Bool)
    (assert (= (f a) b))
    (assert (p a))
    (check-sat)
    (get-model)
  )");
  assert(result.result == CheckSatResult::Sat);
  assert(result.model.find("(model") != std::string::npos);
  assert(result.model.find("(define-fun a () A (as @uc_") != std::string::npos);
  assert(result.model.find("(define-fun b () A (as @uc_") != std::string::npos);
  assert(result.model.find("(define-fun f ((x0 A)) A ") != std::string::npos);
  assert(result.model.find("(define-fun p ((x0 A)) Bool ") != std::string::npos);
  assert(result.model.find("$primo_") == std::string::npos);
}

void get_model_parameters_do_not_shadow_user_symbols() {
  const auto result = check_sat_smtlib(R"(
    (set-option :produce-models true)
    (set-logic QF_UF)
    (declare-sort A 0)
    (declare-fun a () A)
    (declare-fun @primo_model_arg_1 () A)
    (declare-fun x1 (A A) A)
    (assert (= (x1 a a) a))
    (check-sat)
    (get-model)
  )");
  assert(result.result == CheckSatResult::Sat);
  assert(result.model.find("(define-fun x1 ((x0 A) (@primo_model_arg_1_ A)) A") !=
         std::string::npos);
  assert(result.model.find("(= @primo_model_arg_1_ (as @uc_") != std::string::npos);
}

void get_model_preserves_solved_constant_preprocessing() {
  const auto result = check_sat_smtlib(R"(
    (set-option :produce-models true)
    (set-logic QF_UF)
    (declare-sort A 0)
    (declare-fun a () A)
    (declare-fun b () A)
    (declare-fun c () A)
    (declare-fun f (A) A)
    (declare-fun g (A) A)
    (assert (= a (f b)))
    (assert (= (g a) c))
    (check-sat)
    (get-model)
  )");
  assert(result.result == CheckSatResult::Sat);
  assert(result.statistics.get(":preprocessing.solve-eqs.accepted") >= 1);
  assert(result.model.find("(define-fun a () A ") != std::string::npos);
  assert(result.model.find("(define-fun f ((x0 A)) A ") != std::string::npos);
  assert(result.model.find("(define-fun g ((x0 A)) A ") != std::string::npos);
}

void get_model_completes_eliminated_boolean_arguments_before_uf_tables() {
  const auto result = check_sat_smtlib(R"(
    (set-option :produce-models true)
    (set-logic QF_UF)
    (declare-sort U 0)
    (declare-fun p () Bool)
    (declare-fun a () U)
    (declare-fun tb (Bool) U)
    (assert (= (tb p) a))
    (assert (not (distinct (tb true) (tb false))))
    (check-sat)
    (get-model)
  )");
  assert(result.result == CheckSatResult::Sat);
  assert(result.statistics.get(":preprocessing.solve-eqs.accepted") == 1);
  assert(result.typed_model);
  const auto& model = *result.typed_model;
  const auto p = model.apply(0);
  const std::array argument{p};
  assert(model.apply(2, argument) == model.apply(1));
  const std::array yes{primo::core::ModelValue{p.sort, true}};
  const std::array no{primo::core::ModelValue{p.sort, false}};
  assert(model.apply(2, yes) == model.apply(2, no));
}

void unsat_get_model_has_no_model() {
  const auto result = check_sat_smtlib(R"(
    (set-option :produce-models true)
    (set-logic QF_UF)
    (declare-sort A 0)
    (declare-fun a () A)
    (declare-fun b () A)
    (assert (= a b))
    (assert (not (= a b)))
    (check-sat)
    (get-model)
  )");
  assert(result.result == CheckSatResult::Unsat);
  assert(result.model.empty());
}

void phase_timing_stats_are_reported() {
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_UF)
    (declare-fun p () Bool)
    (assert p)
    (check-sat)
  )");
  assert(result.result == CheckSatResult::Sat);
  assert(result.statistics.contains(":time.parse-us"));
  assert(result.statistics.contains(":time.frontend-us"));
  assert(result.statistics.contains(":time.preprocessing-us"));
  assert(result.statistics.contains(":time.solve-us"));
  assert(result.statistics.contains(":time.total-us"));
  assert(result.statistics.get(":time.total-us") >= result.statistics.get(":time.parse-us"));
  assert(result.statistics.get(":time.total-us") >= result.statistics.get(":time.solve-us"));
}

void top_level_conjunction_does_not_allocate_auxiliary() {
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_UF)
    (declare-fun p () Bool)
    (declare-fun q () Bool)
    (declare-fun r () Bool)
    (assert (and p q r))
    (check-sat)
  )");
  assert(result.result == CheckSatResult::Sat);
  assert(result.statistics.get(":preprocessing.boolean-units.facts") >= 3);
  assert(result.statistics.get(":sat.vars") == 3);
  assert(result.statistics.get(":sat.clauses") == 3);
  assert(result.statistics.get(":sat.unit-clauses") == 3);
}

void top_level_or_can_use_tseitin_support_clauses() {
  primo::smt::CheckSatOptions options;
  options.euf_propagation = primo::smt::EufPropagationMode::Off;
  const auto flat = check_sat_smtlib(
      R"(
    (set-logic QF_UF)
    (declare-fun p () Bool)
    (declare-fun q () Bool)
    (assert (or p q))
    (check-sat)
  )",
      options);
  assert(flat.result == CheckSatResult::Sat);
  assert(flat.statistics.get(":sat.vars") == 2);
  assert(flat.statistics.get(":sat.clauses") == 1);
  assert(flat.statistics.get(":sat.binary-clauses") == 1);
  assert(flat.statistics.get(":sat.unit-clauses") == 0);

  options.top_level_or_tseitin = true;
  const auto encoded = check_sat_smtlib(
      R"(
    (set-logic QF_UF)
    (declare-fun p () Bool)
    (declare-fun q () Bool)
    (assert (or p q))
    (check-sat)
  )",
      options);
  assert(encoded.result == CheckSatResult::Sat);
  assert(encoded.statistics.get(":sat.vars") == 3);
  assert(encoded.statistics.get(":sat.clauses") == 4);
  assert(encoded.statistics.get(":sat.binary-clauses") == 2);
  assert(encoded.statistics.get(":sat.unit-clauses") == 1);
}

void auto_top_level_or_tseitin_selects_sparse_negated_finite_domain_shape() {
  std::ostringstream input;
  input << "(set-logic QF_UF)\n";
  input << "(declare-sort A 0)\n";
  input << "(declare-fun a () A)\n";
  input << "(declare-fun b () A)\n";
  input << "(declare-fun p () Bool)\n";
  input << "(declare-fun q () Bool)\n";
  for (int i = 0; i < 60; ++i) {
    input << "(declare-fun x" << i << " () A)\n";
  }
  for (int i = 0; i < 1000; ++i) {
    input << "(declare-fun guard" << i << " () Bool)\n";
  }
  input << "(assert (and (distinct a b)";
  for (int i = 0; i < 60; ++i) {
    input << " (or (= x" << i << " a) (= x" << i << " b))";
  }
  for (int i = 0; i < 1000; ++i) {
    input << " (or guard" << i << " q)";
  }
  input << "))\n(check-sat)\n";

  primo::smt::CheckSatOptions options;
  options.solved_constant_substitution = false;
  const auto selected = check_sat_smtlib(input.str(), options);
  assert(selected.result == CheckSatResult::Sat);
  assert(selected.statistics.get(":sat.top-level-or-tseitin.auto") == 1);
  assert(selected.statistics.get(":preprocessing.boolean-shape.top-level-or-nodes") >= 100);

  options.auto_top_level_or_tseitin = false;
  const auto disabled = check_sat_smtlib(input.str(), options);
  assert(disabled.result == CheckSatResult::Sat);
  assert(disabled.statistics.get(":sat.top-level-or-tseitin.auto") == 0);
}

void pure_boolean_unsat() {
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_UF)
    (declare-fun p () Bool)
    (assert p)
    (assert (not p))
    (check-sat)
  )");
  assert(result.result == CheckSatResult::Unsat);
}

void implication_is_clausified() {
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_UF)
    (declare-fun p () Bool)
    (declare-fun q () Bool)
    (assert p)
    (assert (=> p q))
    (assert (not q))
    (check-sat)
  )");
  assert(result.result == CheckSatResult::Unsat);
}

void implication_is_right_associative() {
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_UF)
    (declare-fun p () Bool)
    (declare-fun q () Bool)
    (declare-fun r () Bool)
    (assert p)
    (assert q)
    (assert (=> p q r))
    (assert (not r))
    (check-sat)
  )");
  assert(result.result == CheckSatResult::Unsat);
}

void xor_is_clausified() {
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_UF)
    (declare-fun p () Bool)
    (declare-fun q () Bool)
    (assert p)
    (assert q)
    (assert (xor p q))
    (check-sat)
  )");
  assert(result.result == CheckSatResult::Unsat);
}

void repeated_negation_reuses_the_child_literal() {
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_UF)
    (declare-fun p () Bool)
    (assert (not p))
    (assert (not p))
    (assert p)
    (check-sat)
  )");
  assert(result.result == CheckSatResult::Unsat);
}

void boolean_equality_is_propositional_equivalence() {
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_UF)
    (declare-fun p () Bool)
    (declare-fun q () Bool)
    (assert (= p q))
    (assert p)
    (assert (not q))
    (check-sat)
  )");
  assert(result.result == CheckSatResult::Unsat);
  assert(result.statistics.get(":preprocessing.boolean-units.facts") >= 2);
  assert(result.statistics.get(":solver.boolean") == 0);
  assert(result.statistics.get(":solver.qf-uf") == 0);
}

void self_contradictory_boolean_equality_is_unsat() {
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_UF)
    (declare-fun p () Bool)
    (assert (= p (not p)))
    (check-sat)
  )");
  assert(result.result == CheckSatResult::Unsat);
}

void boolean_distinct_is_propositional_xor() {
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_UF)
    (declare-fun p () Bool)
    (declare-fun q () Bool)
    (assert (distinct p q))
    (assert p)
    (assert q)
    (check-sat)
  )");
  assert(result.result == CheckSatResult::Unsat);
}

void reflexive_equality_is_sat() {
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_UF)
    (declare-sort A 0)
    (declare-fun a () A)
    (assert (= a a))
    (check-sat)
  )");
  assert(result.result == CheckSatResult::Sat);
}

void pure_euf_disequality_contradiction_is_unsat() {
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_UF)
    (declare-sort A 0)
    (declare-fun a () A)
    (declare-fun b () A)
    (assert (= a b))
    (assert (not (= a b)))
    (check-sat)
  )");
  assert(result.result == CheckSatResult::Unsat);
}

void default_euf_propagation_auto_uses_online_for_small_abstractions() {
  primo::smt::CheckSatOptions options;
  options.solved_constant_substitution = false;
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_UF)
    (declare-sort A 0)
    (declare-fun a () A)
    (declare-fun b () A)
    (assert (= a b))
    (check-sat)
  )",
                                       options);
  assert(result.result == CheckSatResult::Sat);
  assert(result.statistics.get(":euf.propagation.auto-online") == 1);
  assert(result.statistics.get(":euf.propagation.auto-off") == 0);
}

void default_euf_propagation_auto_uses_conflict_for_large_abstractions() {
  std::ostringstream input;
  input << "(set-logic QF_UF)\n";
  input << "(declare-sort A 0)\n";
  input << "(declare-fun a0 () A)\n";
  for (int i = 1; i <= 5001; ++i) {
    input << "(declare-fun a" << i << " () A)\n";
    input << "(assert (= a0 a" << i << "))\n";
  }
  input << "(check-sat)\n";

  primo::smt::CheckSatOptions options;
  options.preprocessing_mode = primo::smt::PreprocessingMode::Off;
  const auto result = check_sat_smtlib(input.str(), options);

  assert(result.result == CheckSatResult::Sat);
  assert(result.statistics.get(":euf.propagation.auto-conflict") == 1);
  assert(result.statistics.get(":euf.propagation.auto-online") == 0);
  assert(result.statistics.get(":euf.propagation.auto-off") == 0);
}

void default_euf_propagation_auto_uses_online_for_large_finite_domain_shapes() {
  std::ostringstream input;
  input << "(set-logic QF_UF)\n";
  input << "(declare-sort A 0)\n";
  input << "(declare-fun v0 () A)\n";
  input << "(declare-fun v1 () A)\n";
  for (int i = 0; i < 501; ++i) {
    input << "(declare-fun x" << i << " () A)\n";
    input << "(assert (or (= x" << i << " v0) (= x" << i << " v1)))\n";
  }
  for (int i = 0; i < 4000; ++i) {
    input << "(declare-fun d" << i << " () A)\n";
  }
  input << "(assert (and (distinct v0 v1)";
  for (int i = 0; i < 501; ++i) {
    input << " (or (= x" << i << " v0) (= x" << i << " v1))";
  }
  input << " (or";
  for (int i = 0; i < 4000; ++i) {
    input << " (= d" << i << " v0)";
  }
  input << ")))\n";
  input << "(check-sat)\n";

  primo::smt::CheckSatOptions options;
  options.solved_constant_substitution = false;
  options.auto_basic_multi_preprocessing = false;
  const auto result = check_sat_smtlib(input.str(), options);

  assert(result.result == CheckSatResult::Sat);
  assert(result.statistics.get(":preprocessing.finite-domain.domains") >= 500);
  assert(result.statistics.get(":euf.theory-atoms") > 5000);
  assert(result.statistics.get(":euf.propagation.auto-online") == 1);
  assert(result.statistics.get(":euf.propagation.auto-conflict") == 0);
}

void default_euf_propagation_auto_keeps_conflict_for_negative_heavy_finite_domain_shapes() {
  std::ostringstream input;
  input << "(set-logic QF_UF)\n";
  input << "(declare-sort A 0)\n";
  input << "(declare-fun v0 () A)\n";
  input << "(declare-fun v1 () A)\n";
  for (int i = 0; i < 501; ++i) {
    input << "(declare-fun x" << i << " () A)\n";
    input << "(assert (or (= x" << i << " v0) (= x" << i << " v1)))\n";
  }
  for (int i = 0; i < 4000; ++i) {
    input << "(declare-fun d" << i << " () A)\n";
  }
  for (int i = 0; i < 600; ++i) {
    input << "(declare-fun n" << i << " () A)\n";
  }
  input << "(assert (and (distinct v0 v1)";
  for (int i = 0; i < 501; ++i) {
    input << " (or (= x" << i << " v0) (= x" << i << " v1))";
  }
  for (int i = 0; i < 600; ++i) {
    input << " (not (= n" << i << " v0))";
  }
  input << " (or";
  for (int i = 0; i < 4000; ++i) {
    input << " (= d" << i << " v0)";
  }
  input << ")))\n";
  input << "(check-sat)\n";

  primo::smt::CheckSatOptions options;
  options.solved_constant_substitution = false;
  options.auto_basic_multi_preprocessing = false;
  const auto result = check_sat_smtlib(input.str(), options);

  assert(result.result == CheckSatResult::Sat);
  assert(result.statistics.get(":preprocessing.finite-domain.domains") >= 500);
  assert(result.statistics.get(":euf.pure-atoms.negative") >
         result.statistics.get(":preprocessing.finite-domain.domains"));
  assert(result.statistics.get(":euf.theory-atoms") > 5000);
  assert(result.statistics.get(":euf.propagation.auto-online") == 0);
  assert(result.statistics.get(":euf.propagation.auto-conflict") == 1);
}

void chainable_equality_is_expanded() {
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_UF)
    (declare-sort A 0)
    (declare-fun a () A)
    (declare-fun b () A)
    (declare-fun c () A)
    (assert (= a b c))
    (assert (not (= a c)))
    (check-sat)
  )");
  assert(result.result == CheckSatResult::Unsat);
}

void let_bound_terms_are_resolved() {
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_UF)
    (declare-sort A 0)
    (declare-fun a () A)
    (declare-fun f (A) A)
    (assert (let ((?x (f a))) (= ?x ?x)))
    (check-sat)
  )");
  assert(result.result == CheckSatResult::Sat);
}

void distinct_lowers_to_pairwise_disequalities() {
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_UF)
    (declare-sort A 0)
    (declare-fun a () A)
    (assert (distinct a a))
    (check-sat)
  )");
  assert(result.result == CheckSatResult::Unsat);
}

void mixed_boolean_and_euf_can_be_sat() {
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_UF)
    (declare-sort A 0)
    (declare-fun a () A)
    (declare-fun b () A)
    (declare-fun p () Bool)
    (assert (or p (= a b)))
    (check-sat)
  )");
  assert(result.result == CheckSatResult::Sat);
}

void mixed_boolean_and_euf_conflict_is_unsat() {
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_UF)
    (declare-sort A 0)
    (declare-fun a () A)
    (declare-fun b () A)
    (declare-fun p () Bool)
    (assert (or p (= a b)))
    (assert (not p))
    (assert (not (= a b)))
    (check-sat)
  )");
  assert(result.result == CheckSatResult::Unsat);
}

void predicate_applications_respect_congruence() {
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_UF)
    (declare-sort A 0)
    (declare-fun a () A)
    (declare-fun b () A)
    (declare-fun p (A) Bool)
    (assert (= a b))
    (assert (p a))
    (assert (not (p b)))
    (check-sat)
  )");
  assert(result.result == CheckSatResult::Unsat);
}

void annotated_assertions_are_solved() {
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_UF)
    (declare-sort Z 0)
    (declare-fun circuit () Bool)
    (declare-fun input () Bool)
    (declare-fun reg () Bool)
    (declare-fun flash () Z)
    (declare-fun nf () Z)
    (assert (! circuit :named hyp1))
    (assert (! (= nf flash) :named hyp2))
    (assert (! (not (or (and input (not reg)) (not input) reg)) :named goal))
    (check-sat)
  )");
  assert(result.result == CheckSatResult::Unsat);
}

void define_fun_macros_are_expanded() {
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_UF)
    (declare-sort A 0)
    (declare-fun a () A)
    (declare-fun b () A)
    (define-fun same ((x A) (y A)) Bool (= x y))
    (define-fun aa () A a)
    (assert (same aa b))
    (assert (not (= a b)))
    (check-sat)
  )");
  assert(result.result == CheckSatResult::Unsat);
}

void nullary_define_fun_is_eagerly_compiled() {
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_UF)
    (declare-sort A 0)
    (declare-fun a () A)
    (declare-fun b () A)
    (define-fun aa () A a)
    (define-fun same ((x A) (y A)) Bool (= x y))
    (assert (and (same aa b) (same aa b) (same aa b)))
    (assert (not (= a b)))
    (check-sat)
  )");
  assert(result.result == CheckSatResult::Unsat);
  assert(result.statistics.get(":frontend.define-fun.nullary-direct-uses") >= 3);
  assert(result.statistics.get(":frontend.define-fun.nullary-eager-compiles") == 1);
  assert(result.statistics.get(":frontend.define-fun.nullary-side-assertions") == 0);
  assert(result.statistics.get(":frontend.define-fun.nullary-cache-hits") >= 2);
  assert(result.statistics.get(":frontend.define-fun.nullary-cache-misses") == 1);
  assert(result.statistics.get(":frontend.define-fun.nullary-cache-entries") == 1);
}

void nullary_define_fun_uses_definition_site_bindings() {
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_UF)
    (declare-sort A 0)
    (declare-fun a () A)
    (declare-fun b () A)
    (define-fun d () A a)
    (define-fun use ((a A)) A d)
    (assert (distinct (use b) a))
    (check-sat)
  )");
  assert(result.result == CheckSatResult::Unsat);
}

void nullary_define_fun_replays_boolean_argument_constraints() {
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_UF)
    (declare-sort A 0)
    (declare-fun p () Bool)
    (declare-fun q () Bool)
    (declare-fun f (Bool) A)
    (define-fun d () A (f (or p q)))
    (assert p)
    (assert (distinct d (f true)))
    (check-sat)
  )");
  assert(result.result == CheckSatResult::Unsat);
  assert(result.statistics.get(":frontend.define-fun.nullary-side-assertions") == 1);
}

// An ite in a nullary define-fun body used to compile into two side assertions
// replayed at each use. It is a term now, so the body compiles to a term and
// preprocessing handles it. The distribution of (= u (ite c a b)) is a term
// rewrite rather than a syntactic one, so it reaches through the definition
// and this needs no auxiliary at all -- which the S-expression rule it
// replaced could not do.
void nullary_define_fun_term_ite_needs_no_auxiliary() {
  // Distribution is off by default (it grows the formula more than the
  // auxiliary is worth), so this opts in: the property under test is that the
  // rule is a *term* rewrite and so reaches through the definition, which the
  // S-expression rule it replaced could not do.
  primo::smt::CheckSatOptions options;
  options.ite_equality_distribution = true;
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_UF)
    (declare-sort A 0)
    (declare-fun p () Bool)
    (declare-fun a () A)
    (declare-fun b () A)
    (define-fun d () A (ite p a b))
    (assert p)
    (assert (distinct d a))
    (check-sat)
  )",
                                       options);
  assert(result.result == CheckSatResult::Unsat);
  assert(result.statistics.get(":frontend.define-fun.nullary-side-assertions") == 0);
  assert(result.statistics.get(":preprocessing.ite.auxiliaries") == 0);
  assert(result.statistics.get(":preprocessing.ite.equality-distributed") == 1);
}

// The rewrites in simplify-extended-terms have to be switchable, or they
// cannot be A/B'd against the lowering they replace. Lowering itself is not
// switchable: the linear core has no case for these kinds, so the same input
// must still solve with the leaf off, only with the auxiliary the rewrite
// would have avoided.
void extended_simplification_can_be_switched_off() {
  constexpr const char* kInput = R"(
    (set-logic QF_UF)
    (declare-sort A 0)
    (declare-fun p () Bool)
    (declare-fun a () A)
    (declare-fun b () A)
    (declare-fun u () A)
    (assert (= u (ite p a b)))
    (assert (distinct u a))
    (assert (distinct u b))
    (check-sat)
  )";

  primo::smt::CheckSatOptions distributing;
  distributing.ite_equality_distribution = true;
  const auto simplified = check_sat_smtlib(kInput, distributing);
  assert(simplified.result == CheckSatResult::Unsat);
  assert(simplified.statistics.get(":preprocessing.ite.equality-distributed") == 1);
  assert(simplified.statistics.get(":preprocessing.ite.auxiliaries") == 0);

  primo::smt::CheckSatOptions options;
  options.extended_simplification = false;
  const auto lowered = check_sat_smtlib(kInput, options);
  assert(lowered.result == CheckSatResult::Unsat);
  assert(lowered.statistics.get(":preprocessing.ite.equality-distributed") == 0);
  assert(lowered.statistics.get(":preprocessing.ite.auxiliaries") == 1);
}

void long_nullary_define_fun_chain_is_compiled_linearly() {
  constexpr int kDefinitions = 20000;
  std::ostringstream input;
  input << "(set-logic QF_UFLRA)\n";
  input << "(declare-sort A 0)\n";
  input << "(declare-fun a () A)\n";
  input << "(declare-fun b () A)\n";
  input << "(declare-fun c () A)\n";
  input << "(declare-fun q () Bool)\n";
  input << "(declare-fun x () Real)\n";
  input << "(declare-fun f (Real) Real)\n";
  input << "(declare-fun g (Bool) A)\n";
  input << "(define-fun d0 () Bool (= a c))\n";
  for (int i = 1; i < kDefinitions; ++i) {
    input << "(define-fun d" << i << " () Bool (=> d" << (i - 1) << " q))\n";
  }
  input << "(assert (= a b))\n";
  input << "(assert (<= (f x) x))\n";
  input << "(assert d" << (kDefinitions - 1) << ")\n";
  input << "(assert (= (g d" << (kDefinitions - 1) << ") a))\n";
  input << "(check-sat)\n";

  const auto result = check_sat_smtlib(input.str());
  assert(result.result == CheckSatResult::Sat);
  assert(result.statistics.get(":frontend.define-fun.nullary-eager-compiles") == kDefinitions);
  assert(result.statistics.get(":frontend.define-fun.nullary-direct-uses") >= kDefinitions);
}

void long_nullary_and_chain_preserves_dag_and_supports_optional_flattening() {
  constexpr int kDefinitions = 5000;
  std::ostringstream input;
  input << "(set-logic QF_UF)\n";
  input << "(declare-fun p () Bool)\n";
  for (int i = 1; i < kDefinitions; ++i) {
    input << "(declare-fun q" << i << " () Bool)\n";
  }
  input << "(define-fun d0 () Bool p)\n";
  for (int i = 1; i < kDefinitions; ++i) {
    input << "(define-fun d" << i << " () Bool (and d" << (i - 1) << " q" << i << "))\n";
  }
  input << "(assert d" << (kDefinitions - 1) << ")\n";
  input << "(check-sat)\n";

  CheckSatOptions default_options;
  default_options.preprocessing_mode = primo::smt::PreprocessingMode::Off;
  const auto default_result = check_sat_smtlib(input.str(), default_options);
  assert(default_result.result == CheckSatResult::Sat);
  assert(default_result.statistics.get(":preprocessing.boolean-flatten.attempts") == 0);
  assert(default_result.statistics.get(":preprocessing.boolean-flatten.operands-copied") == 0);

  CheckSatOptions flattened_options = default_options;
  flattened_options.boolean_flatten_threshold = 8;
  const auto flattened_result = check_sat_smtlib(input.str(), flattened_options);
  assert(flattened_result.result == CheckSatResult::Sat);
  assert(flattened_result.statistics.get(":preprocessing.boolean-flatten.accepted") > 0);
  assert(flattened_result.statistics.get(":preprocessing.boolean-flatten.threshold-stops") > 0);
  assert(flattened_result.statistics.get(":preprocessing.boolean-flatten.max-arity") <= 8);
}

void reconvergent_nullary_and_dag_is_clausified_once() {
  constexpr int kDefinitions = 60;
  std::ostringstream input;
  input << "(set-logic QF_UF)\n";
  input << "(declare-fun p () Bool)\n";
  input << "(declare-fun q () Bool)\n";
  input << "(declare-fun r () Bool)\n";
  input << "(define-fun d0 () Bool (or p q))\n";
  input << "(define-fun d1 () Bool (or q r))\n";
  for (int i = 2; i < kDefinitions; ++i) {
    input << "(define-fun d" << i << " () Bool (and d" << (i - 1) << " d" << (i - 2) << "))\n";
  }
  input << "(assert d" << (kDefinitions - 1) << ")\n";
  input << "(check-sat)\n";

  CheckSatOptions options;
  options.preprocessing_mode = primo::smt::PreprocessingMode::Off;
  const auto result = check_sat_smtlib(input.str(), options);
  assert(result.result == CheckSatResult::Sat);
  assert(result.statistics.get(":preprocessing.boolean-shape.top-level-or-nodes") == 2);
  assert(result.statistics.get(":sat.clauses") == 2);
  assert(result.statistics.get(":sat.binary-clauses") == 2);
}

void solved_constant_rewrite_preserves_application_symbols_after_define_fun_expansion() {
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_UF)
    (declare-sort A 0)
    (declare-fun a () A)
    (declare-fun b () A)
    (declare-fun c () A)
    (declare-fun f (A) A)
    (declare-fun p (A) Bool)
    (define-fun d0 () A c)
    (define-fun d1 () A (f (f a)))
    (assert (p d1))
    (assert (= a b))
    (assert (= b c))
    (assert (not (= a c)))
    (check-sat)
  )");
  assert(result.result == CheckSatResult::Unsat);
}

void compound_bool_arguments_are_bound_before_euf_flattening() {
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_UF)
    (declare-sort A 0)
    (declare-fun p () Bool)
    (declare-fun q () Bool)
    (declare-fun wrap (Bool) A)
    (assert p)
    (assert (not (= (wrap (or p q)) (wrap (or p q)))))
    (check-sat)
  )");
  assert(result.result == CheckSatResult::Unsat);
}

void recursive_define_fun_is_rejected() {
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_UF)
    (define-fun loop () Bool loop)
    (assert loop)
    (check-sat)
  )");
  assert(result.result == CheckSatResult::Unsupported);
}

void boolean_ite_is_eliminated() {
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_UF)
    (declare-fun c () Bool)
    (declare-fun p () Bool)
    (declare-fun q () Bool)
    (assert c)
    (assert p)
    (assert (not (ite c p q)))
    (check-sat)
  )");
  assert(result.result == CheckSatResult::Unsat);
}

void term_ite_is_eliminated() {
  primo::smt::CheckSatOptions options;
  options.ite_equality_distribution = true;
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_UF)
    (declare-sort U 0)
    (declare-fun c () Bool)
    (declare-fun a () U)
    (declare-fun b () U)
    (declare-fun x () U)
    (assert c)
    (assert (= x (ite c a b)))
    (assert (not (= x a)))
    (check-sat)
  )",
                                       options);
  assert(result.result == CheckSatResult::Unsat);
  // Distributing the equality over the branches avoids the auxiliary entirely.
  // Off by default, so this opts in; the default path lowers to one auxiliary.
  assert(result.statistics.get(":preprocessing.ite.auxiliaries") == 0);
}

void true_boolean_constant_used_as_uf_argument_is_euf_term() {
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_UF)
    (declare-sort U 0)
    (declare-fun p () Bool)
    (declare-fun f (Bool) U)
    (assert p)
    (assert (not (= (f p) (f true))))
    (check-sat)
  )");
  assert(result.result == CheckSatResult::Unsat);
}

void false_boolean_constant_used_as_uf_argument_is_euf_term() {
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_UF)
    (declare-sort U 0)
    (declare-fun p () Bool)
    (declare-fun f (Bool) U)
    (assert (not p))
    (assert (not (= (f p) (f false))))
    (check-sat)
  )");
  assert(result.result == CheckSatResult::Unsat);
}

void equality_bridge_solves_diamond_chain() {
  primo::smt::CheckSatOptions options;
  options.eq_bridge = true;
  options.preprocessing_passes = 3;
  const auto result = check_sat_smtlib(
      R"(
    (set-logic QF_UF)
    (declare-sort A 0)
    (declare-fun x0 () A)
    (declare-fun y0 () A)
    (declare-fun z0 () A)
    (declare-fun x1 () A)
    (declare-fun y1 () A)
    (declare-fun z1 () A)
    (declare-fun x2 () A)
    (assert (and
      (or (and (= x0 y0) (= y0 x1)) (and (= x0 z0) (= z0 x1)))
      (or (and (= x1 y1) (= y1 x2)) (and (= x1 z1) (= z1 x2)))
      (not (= x0 x2))))
    (check-sat)
  )",
      options);
  assert(result.result == CheckSatResult::Unsat);
  assert(result.statistics.get(":preprocessing.passes") >= 1);
  assert(result.statistics.get(":preprocessing.eq-bridge.equalities") >= 2);
  assert(result.statistics.get(":preprocessing.unit-equality.consequences") >= 1);
}

void ground_unit_substitution_option_records_rewrites() {
  primo::smt::CheckSatOptions options;
  options.ground_unit_substitution = true;
  options.solved_constant_substitution = false;
  const auto result = check_sat_smtlib(
      R"(
    (set-logic QF_UF)
    (declare-sort A 0)
    (declare-fun a () A)
    (declare-fun b () A)
    (declare-fun c () A)
    (declare-fun f (A) A)
    (assert (= (f a) b))
    (assert (or (= (f a) c) (not (= b c))))
    (check-sat)
  )",
      options);
  assert(result.result == CheckSatResult::Sat);
  assert(result.statistics.get(":preprocessing.unit-equality.substitution-units") >= 1);
  assert(result.statistics.get(":preprocessing.unit-equality.substitutions") >= 1);
  assert(result.statistics.get(":preprocessing.unit-equality.substitution-accepted") >= 1);
}

void guarded_ground_unit_substitution_rejects_tiny_rewrite() {
  primo::smt::CheckSatOptions options;
  options.guarded_ground_unit_substitution = true;
  options.solved_constant_substitution = false;
  const auto result = check_sat_smtlib(
      R"(
    (set-logic QF_UF)
    (declare-sort A 0)
    (declare-fun a () A)
    (declare-fun b () A)
    (declare-fun c () A)
    (declare-fun f (A) A)
    (assert (= (f a) b))
    (assert (not (= b c)))
    (check-sat)
  )",
      options);
  assert(result.result == CheckSatResult::Sat);
  assert(result.statistics.get(":preprocessing.unit-equality.substitutions") == 0);
  assert(result.statistics.get(":preprocessing.unit-equality.substitution-rejected") >= 1);
}

void solved_constant_substitution_records_rewrites_by_default() {
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_UF)
    (declare-sort A 0)
    (declare-fun a () A)
    (declare-fun b () A)
    (declare-fun f (A) A)
    (assert (= a (f b)))
    (assert (= (f a) (f b)))
    (check-sat)
  )");

  assert(result.result == CheckSatResult::Sat);
  assert(result.statistics.get(":preprocessing.solve-eqs.candidates") >= 1);
  assert(result.statistics.get(":preprocessing.solve-eqs.accepted") >= 1);
  assert(result.statistics.get(":preprocessing.solve-eqs.rewrites") >= 1);
}

void solved_constant_substitution_can_be_disabled() {
  primo::smt::CheckSatOptions options;
  options.solved_constant_substitution = false;
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_UF)
    (declare-sort A 0)
    (declare-fun a () A)
    (declare-fun b () A)
    (declare-fun f (A) A)
    (assert (= a (f b)))
    (assert (= (f a) (f b)))
    (check-sat)
  )",
                                       options);

  assert(result.result == CheckSatResult::Sat);
  assert(result.statistics.get(":preprocessing.solve-eqs.candidates") == 0);
  assert(result.statistics.get(":preprocessing.solve-eqs.accepted") == 0);
  assert(result.statistics.get(":preprocessing.solve-eqs.rewrites") == 0);
}

void structural_auto_preprocessing_is_invariant_under_conjunction_packaging() {
  primo::smt::CheckSatOptions options;
  options.preprocessing_mode = primo::smt::PreprocessingMode::Auto;
  options.auto_preprocess_selector = primo::smt::AutoPreprocessSelectorMode::Structural;
  options.auto_preprocess_tuning.finite_domain_min_domains = 1;
  const auto separate = check_sat_smtlib(
      R"(
    (set-logic QF_UF)
    (declare-sort U 0)
    (declare-fun a () U)
    (declare-fun b () U)
    (declare-fun x () U)
    (assert (not (= a b)))
    (assert (or (= x a) (= x b)))
    (check-sat)
  )",
      options);
  const auto packaged = check_sat_smtlib(
      R"(
    (set-logic QF_UF)
    (declare-sort U 0)
    (declare-fun a () U)
    (declare-fun b () U)
    (declare-fun x () U)
    (assert (and (not (= a b)) (or (= x a) (= x b))))
    (check-sat)
  )",
      options);

  assert(separate.result == CheckSatResult::Sat);
  assert(packaged.result == CheckSatResult::Sat);
  for (const char* statistic :
       {":preprocessing.auto.structural.conjunct-occurrences",
        ":preprocessing.auto.structural.unique-conjuncts", ":preprocessing.auto.profile.large-skip",
        ":preprocessing.auto.profile.finite-domain-basic",
        ":preprocessing.auto.profile.compact-clause-basic", ":preprocessing.auto.profile.strong"}) {
    assert(separate.statistics.get(statistic) == packaged.statistics.get(statistic));
  }
  assert(separate.statistics.get(":preprocessing.auto.structural.conjunct-occurrences") == 2);
  assert(separate.statistics.get(":preprocessing.auto.profile.finite-domain-basic") == 1);
}

void structural_auto_preprocessing_applies_limits_to_implicit_conjuncts() {
  primo::smt::CheckSatOptions options;
  options.preprocessing_mode = primo::smt::PreprocessingMode::Auto;
  options.auto_preprocess_selector = primo::smt::AutoPreprocessSelectorMode::Structural;
  options.auto_preprocess_tuning.large_conjunct_limit = 1;
  const auto separate = check_sat_smtlib(
      R"(
    (set-logic QF_UF)
    (declare-fun p () Bool)
    (declare-fun q () Bool)
    (assert p)
    (assert q)
    (check-sat)
  )",
      options);
  const auto packaged = check_sat_smtlib(
      R"(
    (set-logic QF_UF)
    (declare-fun p () Bool)
    (declare-fun q () Bool)
    (assert (and p q))
    (check-sat)
  )",
      options);

  assert(separate.result == CheckSatResult::Sat);
  assert(packaged.result == CheckSatResult::Sat);
  assert(separate.statistics.get(":preprocessing.auto.profile.large-skip") == 1);
  assert(packaged.statistics.get(":preprocessing.auto.profile.large-skip") == 1);
  assert(separate.statistics.get(":preprocessing.passes") == 0);
  assert(packaged.statistics.get(":preprocessing.passes") == 0);
}

void clause_implication_rewrite_removes_nested_tseitin_layer() {
  constexpr const char* input = R"(
    (set-logic QF_UF)
    (declare-fun p () Bool)
    (declare-fun q () Bool)
    (declare-fun r () Bool)
    (declare-fun s () Bool)
    (assert (or s (=> p (or q r))))
    (check-sat)
  )";
  primo::smt::CheckSatOptions off_options;
  off_options.preprocessing_mode = primo::smt::PreprocessingMode::Off;
  const auto off = check_sat_smtlib(input, off_options);

  primo::smt::CheckSatOptions clause_options = off_options;
  clause_options.boolean_implies_rewrite = primo::smt::BooleanImpliesRewriteMode::Clause;
  const auto clause = check_sat_smtlib(input, clause_options);

  assert(off.result == CheckSatResult::Sat);
  assert(clause.result == CheckSatResult::Sat);
  assert(off.statistics.get(":preprocessing.boolean-implies.visited") == 0);
  assert(clause.statistics.get(":preprocessing.boolean-implies.visited") == 1);
  assert(clause.statistics.get(":preprocessing.boolean-implies.rewritten") == 1);
  assert(clause.statistics.get(":sat.vars") < off.statistics.get(":sat.vars"));
  assert(clause.statistics.get(":sat.clauses") < off.statistics.get(":sat.clauses"));
}

void clause_implication_rewrite_respects_post_flattening_limit() {
  primo::smt::CheckSatOptions options;
  options.preprocessing_mode = primo::smt::PreprocessingMode::Off;
  options.boolean_implies_rewrite = primo::smt::BooleanImpliesRewriteMode::Clause;
  options.boolean_flatten_post_threshold = 2;
  const auto result = check_sat_smtlib(
      R"(
    (set-logic QF_UF)
    (declare-fun p () Bool)
    (declare-fun q () Bool)
    (declare-fun r () Bool)
    (assert (=> p (or q r)))
    (check-sat)
  )",
      options);

  assert(result.result == CheckSatResult::Sat);
  assert(result.statistics.get(":preprocessing.boolean-implies.rewritten") == 0);
  assert(result.statistics.get(":preprocessing.boolean-implies.threshold-stops") == 1);
}

void auto_preprocessing_bridges_single_assertion_without_finite_domain_expansion() {
  primo::smt::CheckSatOptions options;
  options.preprocessing_mode = primo::smt::PreprocessingMode::Auto;
  const auto result = check_sat_smtlib(
      R"(
    (set-logic QF_UF)
    (declare-sort A 0)
    (declare-fun x0 () A)
    (declare-fun y0 () A)
    (declare-fun z0 () A)
    (declare-fun x1 () A)
    (declare-fun y1 () A)
    (declare-fun z1 () A)
    (declare-fun x2 () A)
    (assert (and
      (or (and (= x0 y0) (= y0 x1)) (and (= x0 z0) (= z0 x1)))
      (or (and (= x1 y1) (= y1 x2)) (and (= x1 z1) (= z1 x2)))
      (not (= x0 x2))))
    (check-sat)
  )",
      options);
  assert(result.result == CheckSatResult::Unsat);
  assert(result.statistics.get(":preprocessing.auto.single-assertion") == 1);
  assert(result.statistics.get(":preprocessing.auto.strong") == 0);
  assert(result.statistics.get(":preprocessing.eq-bridge.equalities") >= 2);
  assert(result.statistics.get(":preprocessing.equality-definitions") == 0);
}

void auto_preprocessing_skips_large_flat_assertion_batches() {
  primo::smt::CheckSatOptions options;
  options.preprocessing_mode = primo::smt::PreprocessingMode::Auto;
  std::ostringstream input;
  input << "(set-logic QF_UF)\n";
  input << "(declare-fun p () Bool)\n";
  for (int i = 0; i < 10001; ++i) {
    input << "(assert p)\n";
  }
  input << "(check-sat)\n";

  const auto result = check_sat_smtlib(input.str(), options);

  assert(result.result == CheckSatResult::Sat);
  assert(result.statistics.get(":input.assertions") == 10001);
  assert(result.statistics.get(":preprocessing.auto.large-assertion-skip") == 1);
  assert(result.statistics.get(":preprocessing.passes") == 0);
}

void preprocessing_off_disables_basic_loop() {
  primo::smt::CheckSatOptions options;
  options.preprocessing_mode = primo::smt::PreprocessingMode::Off;
  const auto result = check_sat_smtlib(
      R"(
    (set-logic QF_UF)
    (declare-fun p () Bool)
    (assert p)
    (check-sat)
  )",
      options);
  assert(result.result == CheckSatResult::Sat);
  assert(result.statistics.get(":preprocessing.passes") == 0);
}

void preprocessing_extends_when_passes_keep_shrinking_terms() {
  primo::smt::CheckSatOptions options;
  options.preprocessing_mode = primo::smt::PreprocessingMode::Basic;
  options.preprocessing_passes = 1;
  const auto result = check_sat_smtlib(
      R"(
    (set-logic QF_UF)
    (declare-fun p () Bool)
    (declare-fun q0 () Bool)
    (declare-fun q1 () Bool)
    (declare-fun q2 () Bool)
    (declare-fun q3 () Bool)
    (declare-fun q4 () Bool)
    (assert (and p
      (or p q0)
      (or p q1)
      (or p q2)
      (or p q3)
      (or p q4)))
    (check-sat)
  )",
      options);
  assert(result.result == CheckSatResult::Sat);
  assert(result.statistics.get(":preprocessing.adaptive-passes") == 1);
  assert(result.statistics.get(":preprocessing.passes") == 2);
  assert(result.statistics.get(":preprocessing.term-size.final") <
         result.statistics.get(":preprocessing.term-size.initial"));
}

void arithmetic_payloads_prevent_unsound_symmetry_restrictions() {
  for (const char* path : {"../benchmarks/smoke/qf_uflra/sat_symmetry_numeric_values.smt2",
                           "../benchmarks/smoke/qf_uflra/sat_symmetry_coefficients.smt2",
                           "../benchmarks/smoke/qf_uflra/sat_symmetry_equal_numeric_values.smt2"}) {
    const std::string input = read_text_file(path);
    for (const bool value_precedence : {false, true}) {
      CheckSatOptions options;
      options.finite_domain_value_precedence = value_precedence;
      assert(check_sat_smtlib(input, options).result == CheckSatResult::Sat);
      options.preprocessing_mode = primo::smt::PreprocessingMode::Basic;
      options.preprocessing_passes = 0;
      assert(check_sat_smtlib(input, options).result == CheckSatResult::Sat);
      options.uf_symmetry_breaking = false;
      assert(check_sat_smtlib(input, options).result == CheckSatResult::Sat);
    }
  }
}

void preprocessing_adaptive_extra_passes_can_be_disabled() {
  primo::smt::CheckSatOptions options;
  options.preprocessing_mode = primo::smt::PreprocessingMode::Basic;
  options.preprocessing_passes = 1;
  options.preprocessing_adaptive_extra_passes = 0;
  const auto result = check_sat_smtlib(
      R"(
    (set-logic QF_UF)
    (declare-fun p () Bool)
    (declare-fun q0 () Bool)
    (declare-fun q1 () Bool)
    (assert (and p (or p q0) (or p q1)))
    (check-sat)
  )",
      options);
  assert(result.result == CheckSatResult::Sat);
  assert(result.statistics.get(":preprocessing.adaptive-passes") == 0);
  assert(result.statistics.get(":preprocessing.passes") == 1);
}

void positive_pure_theory_literal_is_filtered_from_euf_check() {
  primo::smt::CheckSatOptions options;
  options.preprocessing_mode = primo::smt::PreprocessingMode::Off;
  options.euf_propagation = primo::smt::EufPropagationMode::Off;
  options.euf_engine = primo::smt::EufEngine::Lazy;
  const auto result = check_sat_smtlib(
      R"(
    (set-logic QF_UF)
    (declare-sort U 0)
    (declare-fun a () U)
    (declare-fun b () U)
    (declare-fun c () U)
    (declare-fun p () Bool)
    (assert p)
    (assert (= a c))
    (assert (= c b))
    (assert (or (= a b) p))
    (check-sat)
  )",
      options);
  assert(result.result == CheckSatResult::Sat);
  assert(result.statistics.get(":euf.pure-atoms.positive") >= 1);
  assert(result.statistics.get(":euf.pure-literals.filtered") >= 1);
  assert(result.statistics.get(":sat.solve-calls") == 1);
}

void pure_theory_literal_filter_can_be_disabled() {
  primo::smt::CheckSatOptions options;
  options.preprocessing_mode = primo::smt::PreprocessingMode::Off;
  options.euf_propagation = primo::smt::EufPropagationMode::Off;
  options.euf_engine = primo::smt::EufEngine::Lazy;
  options.pure_theory_filter = primo::smt::PureTheoryFilterMode::Off;
  const auto result = check_sat_smtlib(
      R"(
    (set-logic QF_UF)
    (declare-sort U 0)
    (declare-fun a () U)
    (declare-fun b () U)
    (declare-fun c () U)
    (declare-fun p () Bool)
    (assert p)
    (assert (= a c))
    (assert (= c b))
    (assert (or (= a b) p))
    (check-sat)
  )",
      options);
  assert(result.result == CheckSatResult::Sat);
  assert(result.statistics.get(":euf.pure-filter.enabled") == 0);
  assert(result.statistics.get(":euf.pure-atoms.positive") >= 1);
  assert(result.statistics.get(":euf.pure-literals.filtered") == 0);
}

void mixed_polarity_theory_literal_is_not_filtered() {
  primo::smt::CheckSatOptions options;
  options.preprocessing_mode = primo::smt::PreprocessingMode::Off;
  options.euf_propagation = primo::smt::EufPropagationMode::Off;
  options.euf_engine = primo::smt::EufEngine::Lazy;
  const auto result = check_sat_smtlib(
      R"(
    (set-logic QF_UF)
    (declare-sort U 0)
    (declare-fun a () U)
    (declare-fun b () U)
    (declare-fun c () U)
    (declare-fun p () Bool)
    (assert p)
    (assert (= a c))
    (assert (= c b))
    (assert (or (= a b) p))
    (assert (or (not (= a b)) p))
    (check-sat)
  )",
      options);
  assert(result.result == CheckSatResult::Sat);
  assert(result.statistics.get(":euf.pure-literals.filtered") == 0);
}

void strong_preprocessing_enables_finite_domain_amo() {
  primo::smt::CheckSatOptions options;
  options.preprocessing_mode = primo::smt::PreprocessingMode::Strong;
  options.euf_propagation = primo::smt::EufPropagationMode::Conflict;
  const auto result = check_sat_smtlib(
      R"(
    (set-logic QF_UF)
    (declare-sort A 0)
    (declare-fun a () A)
    (declare-fun b () A)
    (declare-fun c () A)
    (declare-fun x () A)
    (assert (distinct a b c))
    (assert (or (= x a) (= x b) (= x c)))
    (check-sat)
  )",
      options);
  assert(result.result == CheckSatResult::Sat);
  assert(result.statistics.get(":preprocessing.finite-domain.domains") == 1);
  assert(result.statistics.get(":preprocessing.finite-domain.amo-clauses") == 3);
  assert(result.statistics.get(":preprocessing.finite-domain.raw-choices") == 1);
  assert(result.statistics.get(":preprocessing.finite-domain.raw-amo-clauses") == 3);
  assert(result.statistics.get(":preprocessing.finite-domain.post-amo-clauses") == 3);
  assert(result.statistics.get(":preprocessing.finite-domain.deduped-amo-clauses") == 3);
  assert(result.statistics.get(":preprocessing.ground-congruence-clauses") == 0);
  assert(result.statistics.get(":euf.propagation.rounds") == 0);
}

void finite_domain_raw_amo_is_refreshed_after_forced_atom_simplification() {
  primo::smt::CheckSatOptions options;
  options.preprocessing_mode = primo::smt::PreprocessingMode::Strong;
  const auto result = check_sat_smtlib(
      R"(
    (set-logic QF_UF)
    (declare-sort A 0)
    (declare-fun a () A)
    (declare-fun b () A)
    (declare-fun c () A)
    (declare-fun x () A)
    (assert (distinct a b c))
    (assert (not (= x a)))
    (assert (or (= x a) (= x b) (= x c)))
    (check-sat)
  )",
      options);
  assert(result.result == CheckSatResult::Sat);
  assert(result.statistics.get(":preprocessing.forced-atoms.rewrites") >= 1);
  assert(result.statistics.get(":preprocessing.finite-domain.raw-choices") == 1);
  // Initial analysis remains diagnostic; emission uses the simplified goal.
  assert(result.statistics.get(":preprocessing.finite-domain.raw-cache-refreshes") == 1);
  assert(result.statistics.get(":preprocessing.finite-domain.raw-amo-clauses") == 3);
  assert(result.statistics.get(":preprocessing.finite-domain.post-amo-clauses") == 1);
  assert(result.statistics.get(":preprocessing.finite-domain.amo-clauses") == 1);
  assert(result.statistics.get(":preprocessing.finite-domain.deduped-amo-clauses") == 1);
}

void finite_domain_post_amo_can_be_disabled() {
  primo::smt::CheckSatOptions options;
  options.preprocessing_mode = primo::smt::PreprocessingMode::Strong;
  options.finite_domain_post_amo = false;
  const auto result = check_sat_smtlib(
      R"(
    (set-logic QF_UF)
    (declare-sort A 0)
    (declare-fun a () A)
    (declare-fun b () A)
    (declare-fun c () A)
    (declare-fun x () A)
    (assert (distinct a b c))
    (assert (not (= x a)))
    (assert (or (= x a) (= x b) (= x c)))
    (check-sat)
  )",
      options);
  assert(result.result == CheckSatResult::Sat);
  assert(result.statistics.get(":preprocessing.forced-atoms.rewrites") >= 1);
  // Initial analysis remains diagnostic; emission uses the simplified goal.
  assert(result.statistics.get(":preprocessing.finite-domain.raw-cache-refreshes") == 1);
  assert(result.statistics.get(":preprocessing.finite-domain.raw-amo-clauses") == 3);
  assert(result.statistics.get(":preprocessing.finite-domain.post-amo-clauses") == 0);
  assert(result.statistics.get(":preprocessing.finite-domain.amo-clauses") == 1);
  assert(result.statistics.get(":preprocessing.finite-domain.deduped-amo-clauses") == 0);
}

void finite_domain_amo_uses_forced_disequalities_modulo_forced_equalities() {
  primo::smt::CheckSatOptions options;
  options.preprocessing_mode = primo::smt::PreprocessingMode::Strong;
  const auto result = check_sat_smtlib(
      R"(
    (set-logic QF_UF)
    (declare-sort A 0)
    (declare-fun a () A)
    (declare-fun b () A)
    (declare-fun c () A)
    (declare-fun x () A)
    (declare-fun y () A)
    (declare-fun z () A)
    (assert (distinct a b c))
    (assert (or (= x a) (= x b) (= x c)))
    (assert (or (= y a) (= y b) (= y c)))
    (assert (= y z))
    (assert (not (= x z)))
    (check-sat)
  )",
      options);
  assert(result.result == CheckSatResult::Sat);
  assert(result.statistics.get(":preprocessing.forced-atoms.disequalities") >= 1);
  // Initial analysis remains diagnostic; emission uses the simplified goal.
  assert(result.statistics.get(":preprocessing.finite-domain.raw-cache-refreshes") == 1);
  assert(result.statistics.get(":preprocessing.finite-domain.raw-amo-clauses") == 6);
  assert(result.statistics.get(":preprocessing.finite-domain.post-amo-clauses") == 9);
  assert(result.statistics.get(":preprocessing.finite-domain.amo-clauses") == 9);
  assert(result.statistics.get(":preprocessing.finite-domain.deduped-amo-clauses") == 9);
}

void auto_preprocessing_uses_basic_polarity_for_small_multiple_assertions() {
  primo::smt::CheckSatOptions options;
  options.preprocessing_mode = primo::smt::PreprocessingMode::Auto;
  options.auto_basic_multi_preprocessing = true;
  options.auto_basic_multi_polarity = true;
  const auto result = check_sat_smtlib(
      R"(
    (set-logic QF_UF)
    (declare-sort A 0)
    (declare-fun a () A)
    (declare-fun b () A)
    (declare-fun c () A)
    (declare-fun x () A)
    (assert (distinct a b c))
    (assert (or (= x a) (= x b) (= x c)))
    (check-sat)
  )",
      options);
  assert(result.result == CheckSatResult::Sat);
  assert(result.statistics.get(":preprocessing.auto.single-assertion") == 0);
  assert(result.statistics.get(":preprocessing.auto.strong") == 0);
  assert(result.statistics.get(":preprocessing.auto.basic-multi") == 1);
  assert(result.statistics.get(":preprocessing.finite-domain.domains") == 0);
  assert(result.statistics.get(":preprocessing.finite-domain.amo-clauses") == 0);
  assert(result.statistics.get(":sat.phase.auto-basic-multi-polarity") == 1);
  assert(result.statistics.get(":sat.phase.theory-negative") <
         result.statistics.get(":euf.theory-atoms"));
}

void auto_preprocessing_basic_multi_can_be_disabled() {
  primo::smt::CheckSatOptions options;
  options.preprocessing_mode = primo::smt::PreprocessingMode::Auto;
  options.auto_basic_multi_preprocessing = false;
  const auto result = check_sat_smtlib(
      R"(
    (set-logic QF_UF)
    (declare-sort A 0)
    (declare-fun a () A)
    (declare-fun b () A)
    (declare-fun c () A)
    (declare-fun x () A)
    (assert (distinct a b c))
    (assert (or (= x a) (= x b) (= x c)))
    (check-sat)
  )",
      options);
  assert(result.result == CheckSatResult::Sat);
  assert(result.statistics.get(":preprocessing.auto.basic-multi") == 0);
  assert(result.statistics.get(":preprocessing.auto.strong") == 1);
  assert(result.statistics.get(":preprocessing.finite-domain.domains") == 1);
  assert(result.statistics.get(":preprocessing.finite-domain.amo-clauses") == 3);
  assert(result.statistics.get(":sat.phase.auto-basic-multi-polarity") == 0);
}

void auto_preprocessing_basic_multi_polarity_can_be_disabled() {
  primo::smt::CheckSatOptions options;
  options.preprocessing_mode = primo::smt::PreprocessingMode::Auto;
  options.auto_basic_multi_preprocessing = true;
  options.auto_basic_multi_polarity = false;
  const auto result = check_sat_smtlib(
      R"(
    (set-logic QF_UF)
    (declare-sort A 0)
    (declare-fun a () A)
    (declare-fun b () A)
    (declare-fun c () A)
    (declare-fun x () A)
    (assert (distinct a b c))
    (assert (or (= x a) (= x b) (= x c)))
    (check-sat)
  )",
      options);
  assert(result.result == CheckSatResult::Sat);
  assert(result.statistics.get(":preprocessing.auto.basic-multi") == 1);
  assert(result.statistics.get(":preprocessing.finite-domain.domains") == 0);
  assert(result.statistics.get(":preprocessing.finite-domain.amo-clauses") == 0);
  assert(result.statistics.get(":sat.phase.auto-basic-multi-polarity") == 0);
  assert(result.statistics.get(":sat.phase.theory-negative") ==
         result.statistics.get(":euf.theory-atoms"));
}

void auto_preprocessing_uses_basic_for_selected_single_assertion_domains() {
  std::ostringstream input;
  input << "(set-logic QF_UF)\n";
  input << "(declare-sort A 0)\n";
  input << "(declare-fun g (A) A)\n";
  for (int value = 0; value < 6; ++value) {
    input << "(declare-fun v" << value << " () A)\n";
  }
  for (int variable = 0; variable < 200; ++variable) {
    input << "(declare-fun x" << variable << " () A)\n";
  }
  input << "(assert (and (distinct v0 v1 v2 v3 v4 v5)\n";
  input << "  (distinct (g v0) (g v1) (g v2) (g v3) (g v4) (g v5))\n";
  for (int variable = 0; variable < 200; ++variable) {
    input << "  (or";
    for (int value = 0; value < 6; ++value) {
      if (variable < 6) {
        input << " (= x" << variable << " v" << value << ")";
      } else {
        input << " (= x" << variable << " (g v" << value << "))";
      }
    }
    input << ")\n";
  }
  input << "))\n(check-sat)\n";

  primo::smt::CheckSatOptions options;
  options.preprocessing_mode = primo::smt::PreprocessingMode::Auto;
  const auto result = check_sat_smtlib(input.str(), options);
  assert(result.result == CheckSatResult::Sat);
  assert(result.statistics.get(":preprocessing.auto.basic-single") == 1);
  assert(result.statistics.get(":preprocessing.auto.single-assertion") == 0);
  assert(result.statistics.get(":preprocessing.auto.strong") == 0);
  assert(result.statistics.get(":preprocessing.finite-domain.raw-choices") == 200);
  assert(result.statistics.get(":preprocessing.uf-symmetry.terms") == 6);
  assert(result.statistics.get(":preprocessing.finite-domain.amo-clauses") == 0);
  assert(result.statistics.get(":euf.predicate-atoms") == 0);
  assert(result.statistics.get(":sat.phase.auto-basic-single-predicate-polarity") == 0);
}

void auto_preprocessing_uses_predicate_polarity_for_selected_single_assertion_domains() {
  std::ostringstream input;
  input << "(set-logic QF_UF)\n";
  input << "(declare-sort A 0)\n";
  input << "(declare-fun g (A) A)\n";
  input << "(declare-fun p (A) Bool)\n";
  for (int value = 0; value < 6; ++value) {
    input << "(declare-fun v" << value << " () A)\n";
  }
  for (int variable = 0; variable < 200; ++variable) {
    input << "(declare-fun x" << variable << " () A)\n";
  }
  input << "(assert (and (distinct v0 v1 v2 v3 v4 v5)\n";
  input << "  (distinct (g v0) (g v1) (g v2) (g v3) (g v4) (g v5))\n";
  input << "  (p x0)\n";
  for (int variable = 0; variable < 200; ++variable) {
    input << "  (or";
    for (int value = 0; value < 6; ++value) {
      if (variable < 6) {
        input << " (= x" << variable << " v" << value << ")";
      } else {
        input << " (= x" << variable << " (g v" << value << "))";
      }
    }
    input << ")\n";
  }
  input << "))\n(check-sat)\n";

  primo::smt::CheckSatOptions options;
  options.preprocessing_mode = primo::smt::PreprocessingMode::Auto;
  const auto result = check_sat_smtlib(input.str(), options);
  assert(result.result == CheckSatResult::Sat);
  assert(result.statistics.get(":preprocessing.auto.basic-single") == 1);
  assert(result.statistics.get(":euf.predicate-atoms") == 1);
  assert(result.statistics.get(":sat.phase.auto-basic-single-predicate-polarity") == 1);
  assert(result.statistics.get(":sat.phase.theory-negative") <
         result.statistics.get(":euf.theory-atoms"));
}

void auto_preprocessing_basic_single_can_be_disabled() {
  std::ostringstream input;
  input << "(set-logic QF_UF)\n";
  input << "(declare-sort A 0)\n";
  input << "(declare-fun g (A) A)\n";
  for (int value = 0; value < 6; ++value) {
    input << "(declare-fun v" << value << " () A)\n";
  }
  for (int variable = 0; variable < 200; ++variable) {
    input << "(declare-fun x" << variable << " () A)\n";
  }
  input << "(assert (and (distinct v0 v1 v2 v3 v4 v5)\n";
  input << "  (distinct (g v0) (g v1) (g v2) (g v3) (g v4) (g v5))\n";
  for (int variable = 0; variable < 200; ++variable) {
    input << "  (or";
    for (int value = 0; value < 6; ++value) {
      if (variable < 6) {
        input << " (= x" << variable << " v" << value << ")";
      } else {
        input << " (= x" << variable << " (g v" << value << "))";
      }
    }
    input << ")\n";
  }
  input << "))\n(check-sat)\n";

  primo::smt::CheckSatOptions options;
  options.preprocessing_mode = primo::smt::PreprocessingMode::Auto;
  options.auto_basic_single_preprocessing = false;
  const auto result = check_sat_smtlib(input.str(), options);
  assert(result.result == CheckSatResult::Sat);
  assert(result.statistics.get(":preprocessing.auto.basic-single") == 0);
  assert(result.statistics.get(":preprocessing.auto.single-assertion") == 1);
  assert(result.statistics.get(":preprocessing.finite-domain.amo-clauses") > 0);
}

void auto_preprocessing_emits_guarded_equality_definitions() {
  primo::smt::CheckSatOptions options;
  options.preprocessing_mode = primo::smt::PreprocessingMode::Auto;
  options.auto_basic_multi_preprocessing = false;
  options.auto_finite_domain_equality_definitions = true;
  const auto result = check_sat_smtlib(
      R"(
    (set-logic QF_UF)
    (declare-sort A 0)
    (declare-fun a () A)
    (declare-fun b () A)
    (declare-fun c () A)
    (declare-fun x () A)
    (declare-fun y () A)
    (assert (distinct a b c))
    (assert (or (= x a) (= x b) (= x c)))
    (assert (or (= y a) (= y b) (= y c)))
    (assert (not (= x y)))
    (check-sat)
  )",
      options);
  assert(result.result == CheckSatResult::Sat);
  assert(result.statistics.get(":preprocessing.auto.strong") == 1);
  assert(result.statistics.get(":preprocessing.equality-definitions.candidate-pairs") >= 1);
  assert(result.statistics.get(":preprocessing.equality-definitions.accepted-pairs") >= 1);
  assert(result.statistics.get(":preprocessing.equality-definitions") >= 9);
}

void finite_domain_equality_definitions_can_be_disabled() {
  primo::smt::CheckSatOptions options;
  options.preprocessing_mode = primo::smt::PreprocessingMode::Auto;
  options.auto_basic_multi_preprocessing = false;
  options.auto_finite_domain_equality_definitions = true;
  options.finite_domain_equality_definitions = false;
  const auto result = check_sat_smtlib(
      R"(
    (set-logic QF_UF)
    (declare-sort A 0)
    (declare-fun a () A)
    (declare-fun b () A)
    (declare-fun c () A)
    (declare-fun x () A)
    (declare-fun y () A)
    (assert (distinct a b c))
    (assert (or (= x a) (= x b) (= x c)))
    (assert (or (= y a) (= y b) (= y c)))
    (check-sat)
  )",
      options);
  assert(result.result == CheckSatResult::Sat);
  assert(result.statistics.get(":preprocessing.finite-domain.amo-clauses") >= 6);
  assert(result.statistics.get(":preprocessing.equality-definitions") == 0);
  assert(result.statistics.get(":preprocessing.equality-definitions.candidate-pairs") == 0);
}

void auto_preprocessing_keeps_equality_definitions_opt_in() {
  primo::smt::CheckSatOptions options;
  options.preprocessing_mode = primo::smt::PreprocessingMode::Auto;
  options.auto_basic_multi_preprocessing = false;
  const auto result = check_sat_smtlib(
      R"(
    (set-logic QF_UF)
    (declare-sort A 0)
    (declare-fun a () A)
    (declare-fun b () A)
    (declare-fun c () A)
    (declare-fun x () A)
    (declare-fun y () A)
    (assert (distinct a b c))
    (assert (or (= x a) (= x b) (= x c)))
    (assert (or (= y a) (= y b) (= y c)))
    (check-sat)
  )",
      options);
  assert(result.result == CheckSatResult::Sat);
  assert(result.statistics.get(":preprocessing.finite-domain.amo-clauses") >= 6);
  assert(result.statistics.get(":preprocessing.equality-definitions") == 0);
  assert(result.statistics.get(":preprocessing.equality-definitions.candidate-pairs") == 0);
}

void default_symmetry_breaking_reports_covered_values_and_terms() {
  primo::smt::CheckSatOptions options;
  options.euf_propagation = primo::smt::EufPropagationMode::Conflict;
  const auto result = check_sat_smtlib(
      R"(
    (set-logic QF_UF)
    (declare-sort A 0)
    (declare-fun a () A)
    (declare-fun b () A)
    (declare-fun c () A)
    (declare-fun x () A)
    (declare-fun y () A)
    (declare-fun z () A)
    (declare-fun P (A) Bool)
    (assert (distinct a b c))
    (assert (or (P a) (P b) (P c)))
    (assert (or (= x a) (= x b) (= x c)))
    (assert (or (= y a) (= y b) (= y c)))
    (assert (or (= z a) (= z b) (= z c)))
    (check-sat)
  )",
      options);
  assert(result.result == CheckSatResult::Sat);
  assert(result.statistics.get(":preprocessing.uf-symmetry.accepted-sets") == 1);
  assert(result.statistics.get(":preprocessing.uf-symmetry.values") == 3);
  assert(result.statistics.get(":preprocessing.uf-symmetry.terms") == 3);
  assert(result.statistics.get(":preprocessing.uf-symmetry.constraints") == 6);
  assert(result.statistics.get(":preprocessing.uf-symmetry.value-precedence-terms") == 3);
  assert(result.statistics.get(":preprocessing.uf-symmetry.value-precedence-constraints") == 6);
}

void moving_only_symmetry_terms_are_rejected_from_smtlib() {
  primo::smt::CheckSatOptions options;
  options.euf_propagation = primo::smt::EufPropagationMode::Conflict;
  const auto result = check_sat_smtlib(
      R"(
    (set-logic QF_UF)
    (declare-sort A 0)
    (declare-fun a () A)
    (declare-fun b () A)
    (declare-fun c () A)
    (declare-fun f (A) A)
    (assert (distinct a b c))
    (assert (or (= (f a) a) (= (f a) b) (= (f a) c)))
    (assert (or (= (f b) a) (= (f b) b) (= (f b) c)))
    (assert (or (= (f c) a) (= (f c) b) (= (f c) c)))
    (check-sat)
  )",
      options);
  assert(result.result == CheckSatResult::Sat);
  assert(result.statistics.get(":preprocessing.uf-symmetry.accepted-sets") == 0);
  assert(result.statistics.get(":preprocessing.uf-symmetry.values") == 0);
  assert(result.statistics.get(":preprocessing.uf-symmetry.terms") == 0);
  assert(result.statistics.get(":preprocessing.uf-symmetry.constraints") == 0);
  assert(result.statistics.get(":preprocessing.uf-symmetry.rejected-noninvariant") == 1);
}

void symmetry_ignores_preprocessing_residual_domains() {
  primo::smt::CheckSatOptions options;
  options.euf_propagation = primo::smt::EufPropagationMode::Conflict;
  const auto result = check_sat_smtlib(
      R"(
    (set-logic QF_UF)
    (declare-sort A 0)
    (declare-fun a () A)
    (declare-fun b () A)
    (declare-fun c () A)
    (declare-fun f (A) A)
    (declare-fun x () A)
    (assert (distinct a b c))
    (assert (not (= x a)))
    (assert (or (= x a) (= x b) (= x c)))
    (assert (or (= (f a) a) (= (f a) b) (= (f a) c)))
    (assert (= x c))
    (check-sat)
  )",
      options);
  assert(result.result == CheckSatResult::Sat);
  assert(result.statistics.get(":preprocessing.forced-atoms.rewrites") >= 1);
  assert(result.statistics.get(":preprocessing.uf-symmetry.accepted-sets") == 0);
  assert(result.statistics.get(":preprocessing.uf-symmetry.constraints") == 0);
}

void qg_shape_uses_fixed_terms_but_not_moving_table_terms_for_symmetry() {
  primo::smt::CheckSatOptions options;
  options.euf_propagation = primo::smt::EufPropagationMode::Conflict;
  const auto result = check_sat_smtlib(
      R"(
    (set-logic QF_UF)
    (declare-sort A 0)
    (declare-fun a () A)
    (declare-fun b () A)
    (declare-fun c () A)
    (declare-fun op (A A) A)
    (declare-fun sk1 () A)
    (declare-fun sk2 () A)
    (assert (distinct a b c))
    (assert (or (= (op a a) a) (= (op a a) b) (= (op a a) c)))
    (assert (or (= (op b b) a) (= (op b b) b) (= (op b b) c)))
    (assert (or (= (op c c) a) (= (op c c) b) (= (op c c) c)))
    (assert (or (= sk1 a) (= sk1 b) (= sk1 c)))
    (assert (or (= sk2 a) (= sk2 b) (= sk2 c)))
    (check-sat)
  )",
      options);
  assert(result.result == CheckSatResult::Sat);
  assert(result.statistics.get(":preprocessing.uf-symmetry.accepted-sets") == 1);
  assert(result.statistics.get(":preprocessing.uf-symmetry.values") == 3);
  assert(result.statistics.get(":preprocessing.uf-symmetry.terms") == 2);
  assert(result.statistics.get(":preprocessing.uf-symmetry.constraints") == 4);
  assert(result.statistics.get(":preprocessing.uf-symmetry.value-precedence-terms") == 2);
  assert(result.statistics.get(":preprocessing.uf-symmetry.value-precedence-constraints") == 4);
}

void no_symmetry_breaking_disables_strong_preset_symmetry_only() {
  primo::smt::CheckSatOptions options;
  options.preprocessing_mode = primo::smt::PreprocessingMode::Strong;
  options.euf_propagation = primo::smt::EufPropagationMode::Conflict;
  options.uf_symmetry_breaking = false;
  const auto result = check_sat_smtlib(
      R"(
    (set-logic QF_UF)
    (declare-sort A 0)
    (declare-fun a () A)
    (declare-fun b () A)
    (declare-fun c () A)
    (declare-fun x () A)
    (declare-fun y () A)
    (declare-fun z () A)
    (declare-fun P (A) Bool)
    (assert (distinct a b c))
    (assert (or (P a) (P b) (P c)))
    (assert (or (= x a) (= x b) (= x c)))
    (assert (or (= y a) (= y b) (= y c)))
    (assert (or (= z a) (= z b) (= z c)))
    (check-sat)
  )",
      options);
  assert(result.result == CheckSatResult::Sat);
  assert(result.statistics.get(":preprocessing.finite-domain.domains") == 3);
  assert(result.statistics.get(":preprocessing.finite-domain.amo-clauses") == 9);
  assert(result.statistics.get(":preprocessing.uf-symmetry.accepted-sets") == 0);
  assert(result.statistics.get(":preprocessing.uf-symmetry.values") == 0);
  assert(result.statistics.get(":preprocessing.uf-symmetry.terms") == 0);
  assert(result.statistics.get(":preprocessing.uf-symmetry.constraints") == 0);
}

void theory_phase_option_can_leave_backend_phase_unset() {
  primo::smt::CheckSatOptions options;
  options.theory_phase = primo::smt::TheoryPhaseMode::None;
  const auto result = check_sat_smtlib(
      R"(
    (set-logic QF_UF)
    (declare-sort A 0)
    (declare-fun a () A)
    (declare-fun b () A)
    (assert (= a b))
    (check-sat)
  )",
      options);
  assert(result.result == CheckSatResult::Sat);
  assert(result.statistics.get(":sat.phase.theory-negative") == 0);
}

void default_theory_phase_keeps_negative_phase() {
  primo::smt::CheckSatOptions options;
  options.preprocessing_mode = primo::smt::PreprocessingMode::Strong;
  const auto result = check_sat_smtlib(
      R"(
    (set-logic QF_UF)
    (declare-sort A 0)
    (declare-fun a () A)
    (declare-fun b () A)
    (declare-fun c () A)
    (declare-fun x () A)
    (assert (distinct a b c))
    (assert (or (= x a) (= x b) (= x c)))
    (check-sat)
  )",
      options);
  assert(result.result == CheckSatResult::Sat);
  assert(result.statistics.get(":preprocessing.finite-domain.domains") == 1);
  assert(result.statistics.get(":sat.phase.auto-polarity") == 0);
  assert(result.statistics.get(":sat.phase.theory-negative") ==
         result.statistics.get(":euf.theory-atoms"));
}

void theory_phase_auto_keeps_negative_phase_on_small_balanced_finite_domain_shape() {
  primo::smt::CheckSatOptions options;
  options.preprocessing_mode = primo::smt::PreprocessingMode::Strong;
  options.theory_phase = primo::smt::TheoryPhaseMode::Auto;
  const auto result = check_sat_smtlib(
      R"(
    (set-logic QF_UF)
    (declare-sort A 0)
    (declare-fun a () A)
    (declare-fun b () A)
    (declare-fun c () A)
    (declare-fun x () A)
    (assert (distinct a b c))
    (assert (or (= x a) (= x b) (= x c)))
    (check-sat)
  )",
      options);
  assert(result.result == CheckSatResult::Sat);
  assert(result.statistics.get(":preprocessing.finite-domain.domains") == 1);
  assert(result.statistics.get(":sat.phase.auto-polarity") == 0);
  assert(result.statistics.get(":sat.phase.theory-negative") ==
         result.statistics.get(":euf.theory-atoms"));
}

void theory_phase_auto_keeps_negative_phase_for_positive_dominated_shape() {
  primo::smt::CheckSatOptions options;
  options.preprocessing_mode = primo::smt::PreprocessingMode::Strong;
  options.theory_phase = primo::smt::TheoryPhaseMode::Auto;
  options.solved_constant_substitution = false;
  const auto result = check_sat_smtlib(
      R"(
    (set-logic QF_UF)
    (declare-sort A 0)
    (declare-fun a () A)
    (declare-fun b () A)
    (declare-fun c () A)
    (declare-fun d () A)
    (declare-fun e () A)
    (declare-fun f () A)
    (declare-fun g () A)
    (declare-fun h () A)
    (declare-fun i () A)
    (declare-fun x () A)
    (assert (distinct a b))
    (assert (or (= x a) (= x b)))
    (assert (= c a))
    (assert (= d a))
    (assert (= e a))
    (assert (= f a))
    (assert (= g a))
    (assert (= h a))
    (assert (= i a))
    (check-sat)
  )",
      options);
  assert(result.result == CheckSatResult::Sat);
  assert(result.statistics.get(":preprocessing.finite-domain.domains") == 1);
  assert(result.statistics.get(":sat.phase.auto-polarity") == 0);
  assert(result.statistics.get(":sat.phase.theory-negative") ==
         result.statistics.get(":euf.theory-atoms"));
}

void theory_phase_auto_uses_polarity_for_low_negative_positive_dominated_shape() {
  std::ostringstream input;
  input << "(set-logic QF_UF)\n";
  input << "(declare-sort A 0)\n";
  input << "(declare-fun a () A)\n";
  input << "(declare-fun b () A)\n";
  input << "(declare-fun x () A)\n";
  input << "(assert (distinct a b))\n";
  input << "(assert (or (= x a) (= x b)))\n";
  for (int i = 0; i < 110; ++i) {
    input << "(declare-fun c" << i << " () A)\n";
    input << "(assert (= c" << i << " a))\n";
  }
  input << "(check-sat)\n";

  primo::smt::CheckSatOptions options;
  options.preprocessing_mode = primo::smt::PreprocessingMode::Strong;
  options.theory_phase = primo::smt::TheoryPhaseMode::Auto;
  options.solved_constant_substitution = false;
  const auto result = check_sat_smtlib(input.str(), options);

  assert(result.result == CheckSatResult::Sat);
  assert(result.statistics.get(":preprocessing.finite-domain.domains") == 1);
  assert(result.statistics.get(":euf.pure-atoms.positive") >= 100);
  assert(result.statistics.get(":euf.pure-atoms.negative") <= 25);
  assert(result.statistics.get(":sat.phase.auto-polarity") == 1);
  assert(result.statistics.get(":sat.phase.theory-negative") <
         result.statistics.get(":euf.theory-atoms"));
}

void strong_preprocessing_enables_ground_congruence_clauses() {
  primo::smt::CheckSatOptions options;
  options.preprocessing_mode = primo::smt::PreprocessingMode::Strong;
  options.ground_congruence = true;
  const auto result = check_sat_smtlib(
      R"(
    (set-logic QF_UF)
    (declare-sort A 0)
    (declare-fun a () A)
    (declare-fun b () A)
    (declare-fun f (A) A)
    (assert (= a b))
    (assert (not (= (f a) (f b))))
    (check-sat)
  )",
      options);
  assert(result.result == CheckSatResult::Unsat);
  assert(result.statistics.get(":preprocessing.ground-congruence-clauses") >= 1);
}

void disequality_propagation_diagnostic_counts_syntactic_unary_opportunity() {
  primo::smt::CheckSatOptions options;
  options.preprocessing_mode = primo::smt::PreprocessingMode::Off;
  options.euf_propagation = primo::smt::EufPropagationMode::Off;
  options.pure_theory_filter = primo::smt::PureTheoryFilterMode::Off;
  options.euf_diseq_propagation_diagnostics = true;
  const auto result = check_sat_smtlib(
      R"(
    (set-logic QF_UF)
    (declare-sort A 0)
    (declare-fun a () A)
    (declare-fun b () A)
    (declare-fun f (A) A)
    (assert (not (= (f a) (f b))))
    (assert (not (= a b)))
    (check-sat)
  )",
      options);
  assert(result.result == CheckSatResult::Sat);
  assert(result.statistics.get(":euf.diseq-prop.assigned-app-disequalities") >= 1);
  assert(result.statistics.get(":euf.diseq-prop.source-unary-candidates") == 1);
  assert(result.statistics.get(":euf.diseq-prop.source-unary-existing-arg-atom") == 1);
  assert(result.statistics.get(":euf.diseq-prop.source-unary-arg-atom-false") == 1);
  assert(result.statistics.get(":euf.diseq-prop.flat-same-function-candidates") >= 1);
  assert(result.statistics.get(":euf.diseq-prop.flat-same-function-existing-arg-atom") >= 1);
  assert(result.statistics.get(":euf.diseq-prop.flat-same-function-arg-atom-false") >= 1);
}

void disequality_propagation_diagnostic_counts_flat_curried_opportunity() {
  primo::smt::CheckSatOptions options;
  options.preprocessing_mode = primo::smt::PreprocessingMode::Off;
  options.euf_propagation = primo::smt::EufPropagationMode::Off;
  options.pure_theory_filter = primo::smt::PureTheoryFilterMode::Off;
  options.euf_diseq_propagation_diagnostics = true;
  const auto result = check_sat_smtlib(
      R"(
    (set-logic QF_UF)
    (declare-sort A 0)
    (declare-fun a () A)
    (declare-fun x () A)
    (declare-fun y () A)
    (declare-fun f (A A) A)
    (assert (not (= (f a x) (f a y))))
    (assert (not (= x y)))
    (check-sat)
  )",
      options);
  assert(result.result == CheckSatResult::Sat);
  assert(result.statistics.get(":euf.diseq-prop.assigned-app-disequalities") >= 1);
  assert(result.statistics.get(":euf.diseq-prop.source-unary-candidates") == 0);
  assert(result.statistics.get(":euf.diseq-prop.flat-same-function-candidates") >= 1);
  assert(result.statistics.get(":euf.diseq-prop.flat-same-function-existing-arg-atom") >= 1);
  assert(result.statistics.get(":euf.diseq-prop.flat-same-function-arg-atom-false") >= 1);
}

void disequality_propagation_diagnostic_rejects_unguarded_nary_pairwise_claim() {
  primo::smt::CheckSatOptions options;
  options.preprocessing_mode = primo::smt::PreprocessingMode::Off;
  options.euf_propagation = primo::smt::EufPropagationMode::Off;
  options.pure_theory_filter = primo::smt::PureTheoryFilterMode::Off;
  options.euf_diseq_propagation_diagnostics = true;
  const auto result = check_sat_smtlib(
      R"(
    (set-logic QF_UF)
    (declare-sort A 0)
    (declare-fun a () A)
    (declare-fun b () A)
    (declare-fun x () A)
    (declare-fun y () A)
    (declare-fun f (A A) A)
    (assert (not (= (f a x) (f b y))))
    (assert (not (= a b)))
    (assert (not (= x y)))
    (check-sat)
  )",
      options);
  assert(result.result == CheckSatResult::Sat);
  assert(result.statistics.get(":euf.diseq-prop.assigned-app-disequalities") >= 1);
  assert(result.statistics.get(":euf.diseq-prop.source-unary-candidates") == 0);
  assert(result.statistics.get(":euf.diseq-prop.flat-same-function-candidates") == 0);
  assert(result.statistics.get(":euf.diseq-prop.guarded-function-guard-candidates") == 0);
  assert(result.statistics.get(":euf.diseq-prop.guarded-argument-guard-candidates") == 0);
}

void ackermann_diagnostic_counts_symbol_pair_costs() {
  primo::smt::CheckSatOptions options;
  options.preprocessing_mode = primo::smt::PreprocessingMode::Off;
  options.euf_ackermann_diagnostics = true;
  const auto result = check_sat_smtlib(
      R"(
    (set-logic QF_UF)
    (declare-sort A 0)
    (declare-fun a () A)
    (declare-fun b () A)
    (declare-fun c () A)
    (declare-fun f (A) A)
    (declare-fun g (A A) A)
    (assert (= (f a) (f b)))
    (assert (= (f b) (f c)))
    (assert (= (g a b) (g b c)))
    (check-sat)
  )",
      options);
  assert(result.result == CheckSatResult::Sat);
  assert(result.statistics.get(":euf.ackermann.applications") == 5);
  assert(result.statistics.get(":euf.ackermann.function-symbols") == 2);
  assert(result.statistics.get(":euf.ackermann.eligible-symbols") == 2);
  assert(result.statistics.get(":euf.ackermann.unary-symbols") == 1);
  assert(result.statistics.get(":euf.ackermann.nary-symbols") == 1);
  assert(result.statistics.get(":euf.ackermann.estimated-constraints") == 4);
  assert(result.statistics.get(":euf.ackermann.estimated-equalities") == 9);
  assert(result.statistics.get(":euf.ackermann.estimated-clause-lits") == 9);
  assert(result.statistics.get(":euf.ackermann.max-symbol-applications") == 3);
  assert(result.statistics.get(":euf.ackermann.max-symbol-arity") == 2);
  assert(result.statistics.get(":euf.ackermann.symbols-cost-le-100") == 2);
  assert(result.statistics.get(":euf.ackermann.symbols-cost-le-1000") == 2);
  assert(result.statistics.get(":euf.ackermann.symbols-cost-le-10000") == 2);
}

void ackermann_diagnostic_is_zero_without_flag() {
  primo::smt::CheckSatOptions options;
  options.preprocessing_mode = primo::smt::PreprocessingMode::Off;
  const auto result = check_sat_smtlib(
      R"(
    (set-logic QF_UF)
    (declare-sort A 0)
    (declare-fun a () A)
    (declare-fun b () A)
    (declare-fun f (A) A)
    (assert (= (f a) (f b)))
    (check-sat)
  )",
      options);
  assert(result.result == CheckSatResult::Sat);
  assert(result.statistics.get(":euf.ackermann.applications") == 0);
  assert(result.statistics.get(":euf.ackermann.estimated-equalities") == 0);
  assert(result.statistics.get(":euf.ackermann.symbols-cost-le-100") == 0);
}

void dynamic_ackermann_shadow_counts_full_assignment_conflict_shape() {
  primo::smt::CheckSatOptions options;
  options.euf_dynamic_ackermann_shadow = true;
  options.euf_propagation = primo::smt::EufPropagationMode::Off;
  options.preprocessing_mode = primo::smt::PreprocessingMode::Off;
  const auto result = check_sat_smtlib(
      R"(
    (set-logic QF_UF)
    (declare-sort A 0)
    (declare-fun a () A)
    (declare-fun b () A)
    (declare-fun f (A) A)
    (assert (= a b))
    (assert (not (= (f a) (f b))))
    (check-sat)
  )",
      options);
  assert(result.result == CheckSatResult::Unsat);
  assert(result.statistics.get(":euf.dynamic-ack.shadow.invocations") >= 1);
  assert(result.statistics.get(":euf.dynamic-ack.shadow.result-pairs") >= 1);
  assert(result.statistics.get(":euf.dynamic-ack.shadow.sat-visible-clauses") >= 1);
  assert(result.statistics.get(":euf.dynamic-ack.shadow.conflicting") >= 1);
  assert(result.statistics.get(":euf.dynamic-ack.shadow.result-diseq-assigned") >= 1);
  assert(result.statistics.get(":euf.dynamic-ack.shadow.all-args-eq-assigned") >= 1);
  assert(result.statistics.get(":euf.dynamic-ack.shadow.max-width") == 2);
  assert(result.statistics.get(":euf.dynamic-ack.shadow.width-le-3") >= 1);
}

void dynamic_ackermann_shadow_counts_online_unit_shape() {
  primo::smt::CheckSatOptions options;
  options.euf_dynamic_ackermann_shadow = true;
  options.euf_propagation = primo::smt::EufPropagationMode::Online;
  options.preprocessing_mode = primo::smt::PreprocessingMode::Off;
  options.euf_propagation_interval = 1;
  options.euf_propagation_assign_threshold = 1.0;
  const auto result = check_sat_smtlib(
      R"(
    (set-logic QF_UF)
    (declare-sort A 0)
    (declare-fun a () A)
    (declare-fun b () A)
    (declare-fun f (A) A)
    (assert (not (= (f a) (f b))))
    (assert (or (= a b) (not (= a b))))
    (check-sat)
  )",
      options);
  assert(result.result == CheckSatResult::Sat);
  assert(result.statistics.get(":euf.dynamic-ack.shadow.invocations") >= 1);
  assert(result.statistics.get(":euf.dynamic-ack.shadow.sat-visible-clauses") >= 1);
  assert(result.statistics.get(":euf.dynamic-ack.shadow.unit") >= 1);
}

void dynamic_ackermann_shadow_is_zero_without_flag() {
  primo::smt::CheckSatOptions options;
  options.euf_propagation = primo::smt::EufPropagationMode::Off;
  options.preprocessing_mode = primo::smt::PreprocessingMode::Off;
  const auto result = check_sat_smtlib(
      R"(
    (set-logic QF_UF)
    (declare-sort A 0)
    (declare-fun a () A)
    (declare-fun b () A)
    (declare-fun f (A) A)
    (assert (= a b))
    (assert (not (= (f a) (f b))))
    (check-sat)
  )",
      options);
  assert(result.result == CheckSatResult::Unsat);
  assert(result.statistics.get(":euf.dynamic-ack.shadow.invocations") == 0);
  assert(result.statistics.get(":euf.dynamic-ack.shadow.conflicting") == 0);
}

void diseq_ackermann_propagates_unary_argument_disequality() {
  primo::smt::CheckSatOptions options = propagation_test_options();
  options.euf_propagation = primo::smt::EufPropagationMode::Online;
  options.euf_diseq_ackermann = true;
  options.euf_propagation_delivery.kind = primo::smt::AutoBudget::Kind::Unlimited;
  const auto result = check_sat_smtlib(
      R"(
    (set-logic QF_UF)
    (declare-sort A 0)
    (declare-fun a () A)
    (declare-fun b () A)
    (declare-fun f (A) A)
    (assert (not (= (f a) (f b))))
    (assert (or (= a b) (not (= a b))))
    (check-sat)
  )",
      options);
  assert(result.result == CheckSatResult::Sat);
  assert(result.statistics.get(":euf.diseq-ack.candidates") >= 1);
  assert(result.statistics.get(":euf.diseq-ack.unit") >= 1);
  assert(result.statistics.get(":euf.diseq-ack.propagated") >= 1);
  assert(result.statistics.get(":euf.diseq-ack.returned") >= 1);
  assert(result.statistics.get(":euf.diseq-ack.reason-lits") >= 1);
}

void diseq_ackermann_reports_lazy_conflict() {
  primo::smt::CheckSatOptions options = propagation_test_options();
  options.euf_engine = primo::smt::EufEngine::Lazy;
  options.euf_propagation = primo::smt::EufPropagationMode::Online;
  options.euf_diseq_ackermann = true;
  const auto result = check_sat_smtlib(
      R"(
    (set-logic QF_UF)
    (declare-sort A 0)
    (declare-fun a () A)
    (declare-fun b () A)
    (declare-fun f (A) A)
    (assert (= a b))
    (assert (not (= (f a) (f b))))
    (check-sat)
  )",
      options);
  assert(result.result == CheckSatResult::Unsat);
  assert(result.statistics.get(":euf.diseq-ack.conflict") >= 1);
}

void diseq_ackermann_requires_existing_argument_atom() {
  primo::smt::CheckSatOptions options = propagation_test_options();
  options.euf_propagation = primo::smt::EufPropagationMode::Online;
  options.euf_diseq_ackermann = true;
  const auto result = check_sat_smtlib(
      R"(
    (set-logic QF_UF)
    (declare-sort A 0)
    (declare-fun a () A)
    (declare-fun b () A)
    (declare-fun f (A) A)
    (assert (not (= (f a) (f b))))
    (check-sat)
  )",
      options);
  assert(result.result == CheckSatResult::Sat);
  assert(result.statistics.get(":euf.diseq-ack.candidates") == 0);
  assert(result.statistics.get(":euf.diseq-ack.missing-arg-atom") >= 1);
  assert(result.statistics.get(":euf.diseq-ack.propagated") == 0);
}

void diseq_ackermann_uses_nary_disjunction_not_pairwise_claim() {
  primo::smt::CheckSatOptions options = propagation_test_options();
  options.euf_propagation = primo::smt::EufPropagationMode::Online;
  options.euf_diseq_ackermann = true;
  options.euf_propagation_delivery.kind = primo::smt::AutoBudget::Kind::Unlimited;
  const auto result = check_sat_smtlib(
      R"(
    (set-logic QF_UF)
    (declare-sort A 0)
    (declare-fun a () A)
    (declare-fun b () A)
    (declare-fun x () A)
    (declare-fun y () A)
    (declare-fun f (A A) A)
    (assert (not (= (f a x) (f b y))))
    (assert (= a b))
    (assert (or (= x y) (not (= x y))))
    (check-sat)
  )",
      options);
  assert(result.result == CheckSatResult::Sat);
  assert(result.statistics.get(":euf.diseq-ack.candidates") >= 1);
  assert(result.statistics.get(":euf.diseq-ack.unit") >= 1);
  assert(result.statistics.get(":euf.diseq-ack.propagated") >= 1);
}

void diseq_ackermann_is_zero_without_flag() {
  primo::smt::CheckSatOptions options = propagation_test_options();
  options.euf_propagation = primo::smt::EufPropagationMode::Online;
  const auto result = check_sat_smtlib(
      R"(
    (set-logic QF_UF)
    (declare-sort A 0)
    (declare-fun a () A)
    (declare-fun b () A)
    (declare-fun f (A) A)
    (assert (not (= (f a) (f b))))
    (assert (or (= a b) (not (= a b))))
    (check-sat)
  )",
      options);
  assert(result.result == CheckSatResult::Sat);
  assert(result.statistics.get(":euf.diseq-ack.candidates") == 0);
  assert(result.statistics.get(":euf.diseq-ack.propagated") == 0);
}

void model_euf_propagation_learns_congruence_lemma() {
  primo::smt::CheckSatOptions options = propagation_test_options();
  options.euf_propagation = primo::smt::EufPropagationMode::Model;
  const auto result = check_sat_smtlib(
      R"(
    (set-logic QF_UF)
    (declare-sort A 0)
    (declare-fun a () A)
    (declare-fun b () A)
    (declare-fun f (A) A)
    (declare-fun p () Bool)
    (assert (= a b))
    (assert (or p (= (f a) (f b))))
    (check-sat)
  )",
      options);
  assert(result.result == CheckSatResult::Sat);
  assert(result.statistics.get(":euf.propagation.rounds") == 1);
  assert(result.statistics.get(":euf.propagation.candidates") >= 2);
  assert(result.statistics.get(":euf.propagation.delivered") >= 1);
  assert(result.statistics.get(":euf.propagation.reason-lits") >= 1);
}

void incremental_model_euf_propagation_uses_flat_lookup() {
  primo::smt::CheckSatOptions options = propagation_test_options();
  options.euf_engine = primo::smt::EufEngine::Incremental;
  options.euf_propagation = primo::smt::EufPropagationMode::Model;
  const auto result = check_sat_smtlib(
      R"(
    (set-logic QF_UF)
    (declare-sort A 0)
    (declare-fun a () A)
    (declare-fun b () A)
    (declare-fun f (A) A)
    (declare-fun p () Bool)
    (assert (= a b))
    (assert (or p (= (f a) (f b))))
    (check-sat)
  )",
      options);
  assert(result.result == CheckSatResult::Sat);
  assert(result.statistics.get(":euf.propagation.delivered") >= 1);
  assert(result.statistics.get(":euf.incremental.lookup-hits") >= 1);
  assert(result.statistics.get(":euf.incremental.congruence-merges") >= 1);
}

void model_euf_propagation_handles_predicate_congruence() {
  primo::smt::CheckSatOptions options = propagation_test_options();
  options.euf_propagation = primo::smt::EufPropagationMode::Model;
  const auto result = check_sat_smtlib(
      R"(
    (set-logic QF_UF)
    (declare-sort A 0)
    (declare-fun a () A)
    (declare-fun b () A)
    (declare-fun p (A) Bool)
    (assert (= a b))
    (assert (p a))
    (assert (p b))
    (check-sat)
  )",
      options);
  assert(result.result == CheckSatResult::Sat);
  assert(result.statistics.get(":euf.propagation.delivered") >= 1);
}

void model_euf_propagation_respects_zero_budget() {
  primo::smt::CheckSatOptions options = propagation_test_options();
  options.euf_propagation = primo::smt::EufPropagationMode::Model;
  options.euf_propagation_budget.kind = primo::smt::AutoBudget::Kind::Fixed;
  options.euf_propagation_budget.value = 0;
  const auto result = check_sat_smtlib(
      R"(
    (set-logic QF_UF)
    (declare-sort A 0)
    (declare-fun a () A)
    (declare-fun b () A)
    (declare-fun f (A) A)
    (declare-fun p () Bool)
    (assert (= a b))
    (assert (or p (= (f a) (f b))))
    (check-sat)
  )",
      options);
  assert(result.result == CheckSatResult::Sat);
  assert(result.statistics.get(":euf.propagation.rounds") == 1);
  assert(result.statistics.get(":euf.propagation.candidates") == 0);
  assert(result.statistics.get(":euf.propagation.delivered") == 0);
  assert(result.statistics.get(":euf.propagation.budget-cutoffs") == 1);
}

void online_euf_propagation_learns_congruence_during_search() {
  primo::smt::CheckSatOptions options = propagation_test_options();
  options.euf_propagation = primo::smt::EufPropagationMode::Online;
  const auto result = check_sat_smtlib(
      R"(
    (set-logic QF_UF)
    (declare-sort A 0)
    (declare-fun a () A)
    (declare-fun b () A)
    (declare-fun f (A) A)
    (declare-fun p () Bool)
    (assert (= a b))
    (assert (or p (= (f a) (f b))))
    (check-sat)
  )",
      options);
  assert(result.result == CheckSatResult::Sat);
  assert(result.statistics.get(":euf.propagation.rounds") >= 1);
  assert(result.statistics.get(":euf.propagation.delivered") >= 1);
}

void incremental_online_euf_propagation_uses_flat_lookup() {
  primo::smt::CheckSatOptions options = propagation_test_options();
  options.euf_engine = primo::smt::EufEngine::Incremental;
  options.euf_propagation = primo::smt::EufPropagationMode::Online;
  const auto result = check_sat_smtlib(
      R"(
    (set-logic QF_UF)
    (declare-sort A 0)
    (declare-fun a () A)
    (declare-fun b () A)
    (declare-fun f (A) A)
    (assert (= a b))
    (assert (or (= (f a) (f b)) (not (= (f a) (f b)))))
    (check-sat)
  )",
      options);
  assert(result.result == CheckSatResult::Sat);
  assert(result.statistics.get(":euf.propagation.delivered") >= 1);
  assert(result.statistics.get(":euf.incremental.terms") >= 1);
  assert(result.statistics.get(":euf.incremental.flat-definitions") >= 1);
  assert(result.statistics.get(":euf.incremental.lookup-hits") >= 1);
  assert(result.statistics.get(":euf.incremental.congruence-merges") >= 1);
}

void incremental_online_euf_stats_publish_at_flush() {
  primo::smt::CheckSatOptions options = propagation_test_options();
  options.euf_engine = primo::smt::EufEngine::Incremental;
  options.euf_propagation = primo::smt::EufPropagationMode::Online;
  const auto result = check_sat_smtlib(
      R"(
    (set-logic QF_UF)
    (declare-sort A 0)
    (declare-fun a () A)
    (declare-fun b () A)
    (declare-fun f (A) A)
    (assert (= a b))
    (assert (or (= (f a) (f b)) (not (= (f a) (f b)))))
    (check-sat)
  )",
      options);
  assert(result.result == CheckSatResult::Sat);
  assert(result.statistics.get(":euf.incremental.merges") >= 1);
  assert(result.statistics.get(":euf.incremental.lookup-hits") >= 1);
}

void online_euf_propagation_handles_transitive_reasons() {
  primo::smt::CheckSatOptions options = propagation_test_options();
  options.euf_propagation = primo::smt::EufPropagationMode::Online;
  const auto result = check_sat_smtlib(
      R"(
    (set-logic QF_UF)
    (declare-sort A 0)
    (declare-fun a () A)
    (declare-fun b () A)
    (declare-fun c () A)
    (declare-fun p () Bool)
    (assert (= a b))
    (assert (= b c))
    (assert (or p (= a c)))
    (check-sat)
  )",
      options);
  assert(result.result == CheckSatResult::Sat);
  assert(result.statistics.get(":euf.propagation.delivered") >= 1);
  assert(result.statistics.get(":euf.propagation.reason-lits") >= 2);
}

void online_euf_propagation_handles_predicate_congruence() {
  primo::smt::CheckSatOptions options = propagation_test_options();
  options.euf_propagation = primo::smt::EufPropagationMode::Online;
  const auto result = check_sat_smtlib(
      R"(
    (set-logic QF_UF)
    (declare-sort A 0)
    (declare-fun a () A)
    (declare-fun b () A)
    (declare-fun p (A) Bool)
    (declare-fun q () Bool)
    (assert (= a b))
    (assert (p a))
    (assert (or q (p b)))
    (check-sat)
  )",
      options);
  assert(result.result == CheckSatResult::Sat);
  assert(result.statistics.get(":euf.propagation.delivered") >= 1);
}

void online_euf_propagation_keeps_conflicts_external() {
  primo::smt::CheckSatOptions options;
  options.preprocessing_mode = primo::smt::PreprocessingMode::Off;
  options.euf_propagation = primo::smt::EufPropagationMode::Online;
  const auto result = check_sat_smtlib(
      R"(
    (set-logic QF_UF)
    (declare-sort A 0)
    (declare-fun a () A)
    (declare-fun b () A)
    (declare-fun c () A)
    (assert (= a b))
    (assert (= b c))
    (assert (not (= a c)))
    (check-sat)
  )",
      options);
  assert(result.result == CheckSatResult::Unsat);
  assert(result.statistics.get(":euf.theory-conflicts") >= 1);
}

void conflict_euf_callback_learns_external_conflict_without_propagation() {
  primo::smt::CheckSatOptions options;
  options.preprocessing_mode = primo::smt::PreprocessingMode::Off;
  options.euf_propagation = primo::smt::EufPropagationMode::Conflict;
  const auto result = check_sat_smtlib(
      R"(
    (set-logic QF_UF)
    (declare-sort A 0)
    (declare-fun a () A)
    (declare-fun b () A)
    (declare-fun c () A)
    (declare-fun f (A) A)
    (assert (= a b))
    (assert (= b c))
    (assert (not (= (f a) (f c))))
    (check-sat)
  )",
      options);
  assert(result.result == CheckSatResult::Unsat);
  assert(result.statistics.get(":euf.assignments") >= 3);
  assert(result.statistics.get(":euf.eq-assignments") >= 2);
  assert(result.statistics.get(":euf.diseq-assignments") >= 1);
  assert(result.statistics.get(":euf.theory-conflicts") >= 1);
  assert(result.statistics.get(":euf.propagation.rounds") == 0);
}

void conflict_propagation_final_check_reuses_incremental_closure() {
  primo::smt::CheckSatOptions options;
  options.preprocessing_mode = primo::smt::PreprocessingMode::Off;
  options.euf_propagation = primo::smt::EufPropagationMode::Conflict;
  const auto result = check_sat_smtlib(
      R"(
    (set-logic QF_UF)
    (declare-sort A 0)
    (declare-fun a () A)
    (declare-fun b () A)
    (declare-fun c () A)
    (declare-fun f (A) A)
    (assert (= a b))
    (assert (= (f a) (f b)))
    (assert (not (= b c)))
    (check-sat)
  )",
      options);
  assert(result.result == CheckSatResult::Sat);
  // The online/conflict propagator resolves the assignment internally, so the
  // outer solve_qf_uf loop should reach a verdict without needing a second
  // full theory-check pass over the assignment.
  assert(result.statistics.get(":euf.checks") == 1);
}

void lazy_engine_with_online_propagation_still_sound() {
  // EufEngine::Lazy has no persistent incremental closure, even when the
  // online/conflict propagator is connected (it only owns one for
  // EufEngine::Incremental). The final conflict check must keep using the
  // legacy find_theory_conflict path here rather than the propagator's
  // (absent) incremental closure, or this would unsoundly report Sat.
  primo::smt::CheckSatOptions options;
  options.preprocessing_mode = primo::smt::PreprocessingMode::Off;
  options.euf_engine = primo::smt::EufEngine::Lazy;
  options.euf_propagation = primo::smt::EufPropagationMode::Online;
  const auto result = check_sat_smtlib(
      R"(
    (set-logic QF_UF)
    (declare-sort A 0)
    (declare-fun a () A)
    (declare-fun b () A)
    (declare-fun c () A)
    (assert (= a b))
    (assert (= b c))
    (assert (not (= a c)))
    (check-sat)
  )",
      options);
  assert(result.result == CheckSatResult::Unsat);
}

void theory_conflict_limit_stops_lazy_search_with_unknown() {
  primo::smt::CheckSatOptions options;
  options.preprocessing_mode = primo::smt::PreprocessingMode::Off;
  options.euf_propagation = primo::smt::EufPropagationMode::Off;
  options.max_theory_conflicts = 1;
  const auto result = check_sat_smtlib(
      R"(
    (set-logic QF_UF)
    (declare-sort A 0)
    (declare-fun a () A)
    (declare-fun b () A)
    (declare-fun c () A)
    (assert (= a b))
    (assert (= b c))
    (assert (not (= a c)))
    (check-sat)
  )",
      options);
  assert(result.result == CheckSatResult::Unknown);
  assert(result.message == "theory conflict limit reached");
  assert(result.reason == primo::smt::InconclusiveReason::ResourceLimit);
  assert(result.statistics.get(":euf.theory-conflicts") == 1);
}

void theory_conflict_limit_stops_external_search_with_unknown() {
  primo::smt::CheckSatOptions options;
  options.preprocessing_mode = primo::smt::PreprocessingMode::Off;
  options.euf_propagation = primo::smt::EufPropagationMode::Conflict;
  options.max_theory_conflicts = 1;
  const auto result = check_sat_smtlib(
      R"(
    (set-logic QF_UF)
    (declare-sort A 0)
    (declare-fun a () A)
    (declare-fun b () A)
    (declare-fun c () A)
    (assert (= a b))
    (assert (= b c))
    (assert (not (= a c)))
    (check-sat)
  )",
      options);
  assert(result.result == CheckSatResult::Unknown);
  assert(result.message == "theory conflict limit reached");
  assert(result.reason == primo::smt::InconclusiveReason::ResourceLimit);
  assert(result.statistics.get(":euf.theory-conflicts") == 1);
  assert(result.statistics.get(":euf.assignments") >= 3);
  assert(result.statistics.contains(":sat.conflicts"));
  assert(result.statistics.contains(":sat.decisions"));
  assert(result.statistics.contains(":sat.propagations"));
  assert(result.statistics.get(":sat.propagations") >= 1);
}

void qf_uf_basic_polarity_portfolio_returns_first_success() {
  primo::smt::CheckSatOptions options;
  options.qf_uf_portfolio = primo::smt::QfUfPortfolioMode::BasicPolarity;
  options.qf_uf_portfolio_conflicts = 10;
  const auto result = check_sat_smtlib(
      R"(
    (set-logic QF_UF)
    (declare-sort A 0)
    (declare-fun a () A)
    (declare-fun b () A)
    (assert (= a b))
    (check-sat)
  )",
      options);
  assert(result.result == CheckSatResult::Sat);
  assert(result.statistics.get(":portfolio.qf-uf.attempts") == 1);
  assert(result.statistics.get(":portfolio.qf-uf.basic-polarity.solved") == 1);
}

void qf_uf_basic_polarity_portfolio_falls_back_after_conflict_budget() {
  primo::smt::CheckSatOptions options;
  options.qf_uf_portfolio = primo::smt::QfUfPortfolioMode::BasicPolarity;
  options.qf_uf_portfolio_conflicts = 0;
  const auto result = check_sat_smtlib(
      R"(
    (set-logic QF_UF)
    (declare-sort A 0)
    (declare-fun a () A)
    (declare-fun b () A)
    (declare-fun c () A)
    (assert (or (= a b) (= b c)))
    (assert (or (not (= a b)) (= b c)))
    (assert (or (= a b) (not (= b c))))
    (assert (not (= a c)))
    (check-sat)
  )",
      options);
  assert(result.result == CheckSatResult::Unsat);
  assert(result.statistics.get(":portfolio.qf-uf.attempts") == 2);
  assert(result.statistics.get(":portfolio.qf-uf.basic-polarity.fell-back") == 1);
  assert(result.statistics.get(":portfolio.qf-uf.basic-polarity.first-conflicts") == 0);
}

void online_euf_propagation_accepts_already_assigned_implications() {
  primo::smt::CheckSatOptions options = propagation_test_options();
  options.euf_propagation = primo::smt::EufPropagationMode::Online;
  const auto result = check_sat_smtlib(
      R"(
    (set-logic QF_UF)
    (declare-sort A 0)
    (declare-fun a () A)
    (declare-fun b () A)
    (declare-fun f (A) A)
    (assert (= a b))
    (assert (= (f a) (f b)))
    (check-sat)
  )",
      options);
  assert(result.result == CheckSatResult::Sat);
}

void online_euf_reason_cache_survives_backtracking() {
  primo::smt::CheckSatOptions options;
  options.euf_propagation = primo::smt::EufPropagationMode::Online;
  options.euf_propagation_interval = 32;
  options.euf_propagation_assign_threshold = 1.0;
  options.euf_propagation_delivery.kind = primo::smt::AutoBudget::Kind::Fixed;
  options.euf_propagation_delivery.value = 1000;
  const std::string input =
      read_text_file("../benchmarks/regression/qf_uf/online_reason_cache.smt2");
  const auto result = check_sat_smtlib(input, options);
  assert(result.result == CheckSatResult::Sat);
  assert(result.statistics.get(":euf.propagation.delivered") >= 1);
}

void boolean_unit_simplification_handles_nested_rewrites() {
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_UF)
    (declare-fun p () Bool)
    (declare-fun q () Bool)
    (declare-fun r () Bool)
    (assert p)
    (assert (= (= p q) r))
    (check-sat)
  )");
  assert(result.result == CheckSatResult::Sat);
  assert(result.statistics.get(":preprocessing.boolean-units.rewrites") >= 1);
}

// The 2026-08-26 cardinality bug: Bool has exactly two elements, so a
// Bool-sorted ARGUMENT is congruent with true or false, and congruence closure
// is incomplete without that fact. These four cases are pure QF_UF -- no
// arithmetic -- because the mixed path had the fact all along and the pure
// path did not, which is precisely how the gap survived nineteen days after
// docs/bool-argument-congruence-bug.md's fix.

void bool_argument_survives_the_pure_theory_filter() {
  // `p` occurs at one polarity propositionally, in `(or p q)`, and also as an
  // argument of `tb`. Polarity is collected from formula structure only and
  // never descends into arguments, so `p` looked pure to the pure-theory
  // filter, which dropped exactly the assignment congruence needed. Found by
  // code review after the first version of this fix shipped; the fix answered
  // it `sat`, and `main` before the fix answered the QF_UFLRA form `unsat`,
  // so it was a regression as well as a gap.
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_UF)
    (declare-sort U 0)
    (declare-fun p () Bool)
    (declare-fun q () Bool)
    (declare-fun tb (Bool) U)
    (assert (= (tb true) (tb false)))
    (assert (not (= (tb p) (tb true))))
    (assert (or p q))
    (check-sat)
  )");
  assert(result.result == CheckSatResult::Unsat);
}

void bool_argument_survives_the_filter_at_negative_polarity() {
  // The mirror: purely-negative occurrence, dropped by the other half of the
  // filter's condition.
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_UF)
    (declare-sort U 0)
    (declare-fun p () Bool)
    (declare-fun q () Bool)
    (declare-fun tb (Bool) U)
    (assert (= (tb true) (tb false)))
    (assert (not (= (tb p) (tb true))))
    (assert (or (not p) q))
    (check-sat)
  )");
  assert(result.result == CheckSatResult::Unsat);
}

void bool_argument_domain_is_two_valued_in_qf_uf() {
  // The smallest witness: tb collapses both elements of Bool, so tb(p) and
  // tb(q) cannot differ whatever p and q are. No Bool-valued function is
  // involved, which isolates the missing fact to argument position.
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_UF)
    (declare-sort U 0)
    (declare-fun p () Bool)
    (declare-fun q () Bool)
    (declare-fun tb (Bool) U)
    (assert (= (tb true) (tb false)))
    (assert (not (= (tb p) (tb q))))
    (check-sat)
  )");
  assert(result.result == CheckSatResult::Unsat);
  assert(result.statistics.get(":no.boolean-domain.registered-terms") == 2);
}

void bool_argument_converse_of_congruence_in_qf_uf() {
  // f is injective on Bool, so (= (f p) (f q)) forces p = q -- the CONVERSE of
  // congruence, which holds only because the domain has two elements. g then
  // turns that derived equality into a contradiction. Every earlier regression
  // test asserts the argument equality instead of deriving it.
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_UF)
    (declare-fun p () Bool)
    (declare-fun q () Bool)
    (declare-fun f (Bool) Bool)
    (declare-fun g (Bool Bool) Bool)
    (assert (= (f p) (f q)))
    (assert (distinct (f true) (f false)))
    (assert (g p q))
    (assert (not (g q q)))
    (check-sat)
  )");
  assert(result.result == CheckSatResult::Unsat);
}

void bool_argument_two_valued_case_stays_satisfiable() {
  // The failure mode a cardinality axiom has when it is too strong: turning
  // sat into unsat. Invisible if every test in the group is an unsat case, so
  // this one is deliberately satisfiable while still registering arguments.
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_UF)
    (declare-sort U 0)
    (declare-fun p () Bool)
    (declare-fun q () Bool)
    (declare-fun f (Bool) Bool)
    (declare-fun tb (Bool) U)
    (assert (distinct (f true) (f false)))
    (assert (= (f p) (f q)))
    (assert (= (tb p) (tb q)))
    (check-sat)
  )");
  assert(result.result == CheckSatResult::Sat);
  assert(result.statistics.get(":no.boolean-domain.registered-terms") >= 1);
}

void ordinary_predicates_register_no_boolean_arguments() {
  // The cost claim, asserted rather than argued: a formula whose functions
  // take no Bool argument registers nothing, so the QF_UF division pays
  // nothing. The older bridge encoding bridged 4 terms on this same shape.
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_UF)
    (declare-sort U 0)
    (declare-fun a () U)
    (declare-fun b () U)
    (declare-fun P (U) Bool)
    (declare-fun Q (U U) Bool)
    (assert (or (P a) (P b)))
    (assert (or (not (P a)) (Q a b)))
    (assert (= a b))
    (check-sat)
  )");
  assert(result.result == CheckSatResult::Sat);
  assert(result.statistics.get(":no.boolean-domain.registered-terms") == 0);
}

void bool_argument_congruence_preserves_boolean_domain() {
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_UFLRA)
    (declare-fun p () Bool)
    (declare-fun q () Bool)
    (declare-fun f (Bool) Bool)
    (declare-fun g (Bool Bool) Bool)
    (declare-fun x () Real)
    (assert (= (f p) (f q)))
    (assert (not (g q q)))
    (assert (distinct (f false) (f true)))
    (assert (g p q))
    (assert (< 0 x))
    (check-sat)
  )");
  assert(result.result == CheckSatResult::Unsat);
  // Asserts that Bool's two-element domain reached the closure, not which
  // encoding carried it: this counter moved from bridged-terms to
  // registered-terms on 2026-08-26 when both solver paths were unified on
  // registration. The `unsat` above is the property; the counter only rules
  // out reaching it by accident.
  assert(result.statistics.get(":no.boolean-domain.registered-terms") >= 1);
}

void bool_result_and_complement_congruence_preserves_boolean_domain() {
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_UFLRA)
    (declare-fun p () Bool)
    (declare-fun q () Bool)
    (declare-fun f (Bool) Bool)
    (declare-fun x () Real)
    (assert (f p))
    (assert (not (= (f (not p)) (f (not q)))))
    (assert (not (= (f q) false)))
    (assert (< 0 x))
    (check-sat)
  )");
  assert(result.result == CheckSatResult::Unsat);
}

void nested_boolean_formula_equality_uses_euf_proxies() {
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_UFLRA)
    (declare-fun p () Bool)
    (declare-fun q () Bool)
    (declare-fun r () Bool)
    (declare-fun x () Real)
    (assert (= (or p (ite q r p)) (and p p)))
    (assert (<= 0 x))
    (check-sat)
  )");
  assert(result.result == CheckSatResult::Sat);
}

void boolean_application_formula_equality_is_propositional() {
  const std::string input = read_text_file(
      "../benchmarks/seeds/qf_uflra/seed_bool_application_formula_equality_mixed_unsat.smt2");

  // Direct duplicate elimination now lets default preprocessing prove this
  // UNSAT before dispatch. Disable preprocessing here so this remains a mixed
  // dispatcher regression rather than only an answer regression.
  CheckSatOptions dispatch_options;
  dispatch_options.preprocessing_mode = primo::smt::PreprocessingMode::Off;
  const auto dispatch_result = check_sat_smtlib(input, dispatch_options);
  assert(dispatch_result.result == CheckSatResult::Unsat);
  assert(dispatch_result.statistics.get(":solver.qf-uflra") == 1);

  assert(check_sat_smtlib(input).result == CheckSatResult::Unsat);

  CheckSatOptions candidate_options;
  candidate_options.qf_uflra_candidate_final_scan = true;
  assert(check_sat_smtlib(input, candidate_options).result == CheckSatResult::Unsat);

  CheckSatOptions online_options;
  online_options.nelson_oppen_propagation = primo::smt::NelsonOppenPropagationMode::Online;
  online_options.qf_uflra_online_euf = true;
  assert(check_sat_smtlib(input, online_options).result == CheckSatResult::Unsat);
}

void boolean_application_formula_equality_is_propositional_in_qf_uf() {
  const std::string input = read_text_file(
      "../benchmarks/seeds/qf_uflra/seed_bool_application_formula_equality_unsat.smt2");
  CheckSatOptions options;
  options.preprocessing_mode = primo::smt::PreprocessingMode::Off;
  const auto result = check_sat_smtlib(input, options);
  assert(result.result == CheckSatResult::Unsat);
  assert(result.statistics.get(":solver.qf-uf") == 1);
  assert(check_sat_smtlib(input).result == CheckSatResult::Unsat);
}

void boolean_application_formula_disequality_is_propositional() {
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_UFLRA)
    (declare-fun p () Bool)
    (declare-fun q () Bool)
    (declare-fun g (Bool Bool) Bool)
    (declare-fun x () Real)
    (assert (< 0 x))
    (assert (g q p))
    (assert (distinct (<= x x) (or (g q p) (g q p))))
    (check-sat)
  )");
  assert(result.result == CheckSatResult::Unsat);
}

void opensmt_boolean_disagreement_is_unsat() {
  const std::string input =
      read_text_file("../benchmarks/regression/qf_uflra/opensmt_boolean_disagreement.smt2");
  const auto result = check_sat_smtlib(input);
  assert(result.result == CheckSatResult::Unsat);
}

void opensmt_boolean_disagreement_original_is_unsat() {
  const std::string input = read_text_file(
      "../benchmarks/regression/qf_uflra/opensmt_boolean_disagreement_original.smt2");
  const auto result = check_sat_smtlib(input);
  assert(result.result == CheckSatResult::Unsat);
}

}  // namespace

// Congruence must not be dropped for Bool-sorted function arguments.
//
// These assert the DISPATCH as well as the answer, which is the assertion that
// was missing: the wrong answer arose because a formula whose equalities are
// all over Bool has has_equality == false (preprocessing rewrites them) and
// the QF_UF dispatch keyed only on that, so an uninterpreted function reached
// solve_boolean_conjunction and its applications were abstracted to
// independent propositional variables. See
// docs/bool-argument-congruence-bug.md.
void bool_argument_congruence_is_not_dropped() {
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_UF)
    (declare-fun p () Bool)
    (declare-fun q () Bool)
    (declare-fun f (Bool) Bool)
    (assert (= p q))
    (assert (not (= (f p) (f q))))
    (check-sat)
  )");
  assert(result.result == CheckSatResult::Unsat);
  assert(result.statistics.get(":solver.qf-uf") == 1);
  assert(result.statistics.get(":solver.boolean") == 0);
}

// The fix must not simply force unsat: without p = q the two applications are
// unrelated and the formula is satisfiable.
void bool_argument_applications_stay_satisfiable_when_unrelated() {
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_UF)
    (declare-fun p () Bool)
    (declare-fun q () Bool)
    (declare-fun f (Bool) Bool)
    (assert (not (= (f p) (f q))))
    (check-sat)
  )");
  assert(result.result == CheckSatResult::Sat);
  assert(result.statistics.get(":solver.qf-uf") == 1);
}

// Congruence through two applications, so the chain is exercised rather than a
// single merge.
void bool_argument_congruence_chains_through_two_applications() {
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_UF)
    (declare-fun p () Bool)
    (declare-fun q () Bool)
    (declare-fun f (Bool) Bool)
    (assert (= p q))
    (assert (not (= (f (f p)) (f (f q)))))
    (check-sat)
  )");
  assert(result.result == CheckSatResult::Unsat);
  assert(result.statistics.get(":solver.qf-uf") == 1);
}

// A compound Boolean argument, which the frontend normalizes with a fresh
// constant plus a propositional equivalence (see docs/architecture.md). That
// path needs its own coverage.
void compound_bool_argument_congruence_is_not_dropped() {
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_UF)
    (declare-fun p () Bool)
    (declare-fun q () Bool)
    (declare-fun f (Bool) Bool)
    (assert (= p q))
    (assert (not (= (f (and p q)) (f (and q p)))))
    (check-sat)
  )");
  assert(result.result == CheckSatResult::Unsat);
  assert(result.statistics.get(":solver.qf-uf") == 1);
}

// The widening must not be total: a formula with no uninterpreted function
// applications still belongs to the Boolean solver.
void purely_propositional_input_still_uses_the_boolean_solver() {
  const auto result = check_sat_smtlib(R"(
    (set-logic QF_UF)
    (declare-fun p () Bool)
    (declare-fun q () Bool)
    (assert (or p q))
    (assert (or (not p) q))
    (check-sat)
  )");
  assert(result.result == CheckSatResult::Sat);
  assert(result.statistics.get(":solver.boolean") == 1);
  assert(result.statistics.get(":solver.qf-uf") == 0);
}

int main() {
  pure_boolean_sat();
  get_model_requires_produce_models();
  get_model_requires_prior_check_sat();
  get_model_prints_qf_uf_model_for_declared_symbols();
  get_model_parameters_do_not_shadow_user_symbols();
  get_model_preserves_solved_constant_preprocessing();
  get_model_completes_eliminated_boolean_arguments_before_uf_tables();
  unsat_get_model_has_no_model();
  phase_timing_stats_are_reported();
  top_level_conjunction_does_not_allocate_auxiliary();
  top_level_or_can_use_tseitin_support_clauses();
  auto_top_level_or_tseitin_selects_sparse_negated_finite_domain_shape();
  pure_boolean_unsat();
  implication_is_clausified();
  implication_is_right_associative();
  xor_is_clausified();
  repeated_negation_reuses_the_child_literal();
  boolean_equality_is_propositional_equivalence();
  self_contradictory_boolean_equality_is_unsat();
  boolean_distinct_is_propositional_xor();
  reflexive_equality_is_sat();
  pure_euf_disequality_contradiction_is_unsat();
  default_euf_propagation_auto_uses_online_for_small_abstractions();
  default_euf_propagation_auto_uses_conflict_for_large_abstractions();
  default_euf_propagation_auto_uses_online_for_large_finite_domain_shapes();
  default_euf_propagation_auto_keeps_conflict_for_negative_heavy_finite_domain_shapes();
  chainable_equality_is_expanded();
  let_bound_terms_are_resolved();
  distinct_lowers_to_pairwise_disequalities();
  mixed_boolean_and_euf_can_be_sat();
  mixed_boolean_and_euf_conflict_is_unsat();
  predicate_applications_respect_congruence();
  annotated_assertions_are_solved();
  define_fun_macros_are_expanded();
  nullary_define_fun_is_eagerly_compiled();
  nullary_define_fun_uses_definition_site_bindings();
  nullary_define_fun_replays_boolean_argument_constraints();
  nullary_define_fun_term_ite_needs_no_auxiliary();
  extended_simplification_can_be_switched_off();
  long_nullary_define_fun_chain_is_compiled_linearly();
  long_nullary_and_chain_preserves_dag_and_supports_optional_flattening();
  reconvergent_nullary_and_dag_is_clausified_once();
  solved_constant_rewrite_preserves_application_symbols_after_define_fun_expansion();
  compound_bool_arguments_are_bound_before_euf_flattening();
  recursive_define_fun_is_rejected();
  boolean_ite_is_eliminated();
  term_ite_is_eliminated();
  true_boolean_constant_used_as_uf_argument_is_euf_term();
  false_boolean_constant_used_as_uf_argument_is_euf_term();
  equality_bridge_solves_diamond_chain();
  ground_unit_substitution_option_records_rewrites();
  guarded_ground_unit_substitution_rejects_tiny_rewrite();
  solved_constant_substitution_records_rewrites_by_default();
  solved_constant_substitution_can_be_disabled();
  structural_auto_preprocessing_is_invariant_under_conjunction_packaging();
  structural_auto_preprocessing_applies_limits_to_implicit_conjuncts();
  clause_implication_rewrite_removes_nested_tseitin_layer();
  clause_implication_rewrite_respects_post_flattening_limit();
  auto_preprocessing_bridges_single_assertion_without_finite_domain_expansion();
  auto_preprocessing_skips_large_flat_assertion_batches();
  preprocessing_off_disables_basic_loop();
  preprocessing_extends_when_passes_keep_shrinking_terms();
  arithmetic_payloads_prevent_unsound_symmetry_restrictions();
  preprocessing_adaptive_extra_passes_can_be_disabled();
  positive_pure_theory_literal_is_filtered_from_euf_check();
  pure_theory_literal_filter_can_be_disabled();
  mixed_polarity_theory_literal_is_not_filtered();
  strong_preprocessing_enables_finite_domain_amo();
  finite_domain_raw_amo_is_refreshed_after_forced_atom_simplification();
  finite_domain_post_amo_can_be_disabled();
  finite_domain_amo_uses_forced_disequalities_modulo_forced_equalities();
  auto_preprocessing_uses_basic_polarity_for_small_multiple_assertions();
  auto_preprocessing_basic_multi_can_be_disabled();
  auto_preprocessing_basic_multi_polarity_can_be_disabled();
  auto_preprocessing_uses_basic_for_selected_single_assertion_domains();
  auto_preprocessing_uses_predicate_polarity_for_selected_single_assertion_domains();
  auto_preprocessing_basic_single_can_be_disabled();
  auto_preprocessing_emits_guarded_equality_definitions();
  finite_domain_equality_definitions_can_be_disabled();
  auto_preprocessing_keeps_equality_definitions_opt_in();
  default_symmetry_breaking_reports_covered_values_and_terms();
  moving_only_symmetry_terms_are_rejected_from_smtlib();
  symmetry_ignores_preprocessing_residual_domains();
  qg_shape_uses_fixed_terms_but_not_moving_table_terms_for_symmetry();
  no_symmetry_breaking_disables_strong_preset_symmetry_only();
  theory_phase_option_can_leave_backend_phase_unset();
  default_theory_phase_keeps_negative_phase();
  theory_phase_auto_keeps_negative_phase_on_small_balanced_finite_domain_shape();
  theory_phase_auto_keeps_negative_phase_for_positive_dominated_shape();
  theory_phase_auto_uses_polarity_for_low_negative_positive_dominated_shape();
  strong_preprocessing_enables_ground_congruence_clauses();
  disequality_propagation_diagnostic_counts_syntactic_unary_opportunity();
  disequality_propagation_diagnostic_counts_flat_curried_opportunity();
  disequality_propagation_diagnostic_rejects_unguarded_nary_pairwise_claim();
  ackermann_diagnostic_counts_symbol_pair_costs();
  ackermann_diagnostic_is_zero_without_flag();
  dynamic_ackermann_shadow_counts_full_assignment_conflict_shape();
  dynamic_ackermann_shadow_counts_online_unit_shape();
  dynamic_ackermann_shadow_is_zero_without_flag();
  diseq_ackermann_propagates_unary_argument_disequality();
  diseq_ackermann_reports_lazy_conflict();
  diseq_ackermann_requires_existing_argument_atom();
  diseq_ackermann_uses_nary_disjunction_not_pairwise_claim();
  diseq_ackermann_is_zero_without_flag();
  model_euf_propagation_learns_congruence_lemma();
  incremental_model_euf_propagation_uses_flat_lookup();
  model_euf_propagation_handles_predicate_congruence();
  model_euf_propagation_respects_zero_budget();
  online_euf_propagation_learns_congruence_during_search();
  incremental_online_euf_propagation_uses_flat_lookup();
  incremental_online_euf_stats_publish_at_flush();
  online_euf_propagation_handles_transitive_reasons();
  online_euf_propagation_handles_predicate_congruence();
  online_euf_propagation_keeps_conflicts_external();
  conflict_euf_callback_learns_external_conflict_without_propagation();
  conflict_propagation_final_check_reuses_incremental_closure();
  lazy_engine_with_online_propagation_still_sound();
  theory_conflict_limit_stops_lazy_search_with_unknown();
  theory_conflict_limit_stops_external_search_with_unknown();
  qf_uf_basic_polarity_portfolio_returns_first_success();
  qf_uf_basic_polarity_portfolio_falls_back_after_conflict_budget();
  online_euf_propagation_accepts_already_assigned_implications();
  online_euf_reason_cache_survives_backtracking();
  boolean_unit_simplification_handles_nested_rewrites();
  bool_argument_congruence_preserves_boolean_domain();
  bool_result_and_complement_congruence_preserves_boolean_domain();
  nested_boolean_formula_equality_uses_euf_proxies();
  boolean_application_formula_equality_is_propositional();
  boolean_application_formula_equality_is_propositional_in_qf_uf();
  boolean_application_formula_disequality_is_propositional();
  opensmt_boolean_disagreement_is_unsat();
  opensmt_boolean_disagreement_original_is_unsat();
  bool_argument_congruence_is_not_dropped();
  bool_argument_applications_stay_satisfiable_when_unrelated();
  bool_argument_congruence_chains_through_two_applications();
  compound_bool_argument_congruence_is_not_dropped();
  purely_propositional_input_still_uses_the_boolean_solver();
  bool_argument_domain_is_two_valued_in_qf_uf();
  bool_argument_converse_of_congruence_in_qf_uf();
  bool_argument_two_valued_case_stays_satisfiable();
  ordinary_predicates_register_no_boolean_arguments();
  bool_argument_survives_the_pure_theory_filter();
  bool_argument_survives_the_filter_at_negative_polarity();
  return 0;
}
