#ifdef NDEBUG
#undef NDEBUG
#endif

#include <cassert>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "core/ids.hpp"
#include "core/term_manager.hpp"
#include "smt/check_sat.hpp"
#include "smt/frontend.hpp"
#include "smt/root_bound_diagnostics.hpp"
#include "smt/smt_session.hpp"
#include "smtlib/parser.hpp"
#include "util/statistics.hpp"

namespace {

primo::util::Statistics diagnose(std::string_view body, std::string_view logic = "QF_LIA",
                                 std::string_view sort = "Int") {
  const std::string input = "(set-logic " + std::string(logic) + ")(declare-const x " +
                            std::string(sort) + ")(declare-const y " + std::string(sort) +
                            ")(declare-const z " + std::string(sort) + ")(declare-const p Bool)" +
                            std::string(body);
  primo::smt::Frontend frontend;
  std::vector<primo::core::TermId> assertions;
  for (const auto& command : primo::smtlib::Parser(input).parse_commands()) {
    frontend.process_command(command, assertions);
  }
  const auto size = frontend.terms().size();
  const auto original = assertions;
  primo::util::Statistics statistics;
  primo::smt::record_root_bound_diagnostics(frontend.terms(), assertions, statistics);
  assert(size == frontend.terms().size());
  assert(original == assertions);
  return statistics;
}

auto count(const primo::util::Statistics& stats, std::string_view name) {
  return stats.get(":arithmetic.root-bounds." + std::string(name));
}

void nonnegative_equation_and_rounding() {
  const auto stats = diagnose("(assert (and (>= x 0) (>= y 0) (= (+ (* 3 x) (* 4 y)) 13)))");
  assert(count(stats, "explicit-bounds") == 2);
  assert(count(stats, "stronger-bounds") == 2);
  assert(count(stats, "new-finite-bounds") == 2);
  assert(count(stats, "improved-variables") == 2);
  assert(count(stats, "integer-roundings") == 2);
  assert(count(stats, "missing-premises") == 2);
  const auto tight = diagnose(
      "(assert (>= x 0))(assert (>= y 0))(assert (<= x 4))(assert (<= y 3))"
      "(assert (= (+ (* 3 x) (* 4 y)) 13))");
  // With both upper bounds present, the reverse calculations now strengthen
  // both lower bounds to 1. Those are finite-to-finite improvements.
  assert(count(tight, "stronger-bounds") == 2);
  assert(count(tight, "new-finite-bounds") == 0);
}

void signed_coefficients_and_negative_floor() {
  // y >= -2 gives x <= floor(20/3) = 6, stronger than 7.
  auto stats = diagnose(
      "(assert (>= y (- 2)))(assert (<= x 7))"
      "(assert (<= (+ (* 3 x) (* 4 y)) 12))");
  assert(count(stats, "stronger-bounds") == 1);
  stats = diagnose(
      "(assert (>= y (- 2)))(assert (<= x 6))"
      "(assert (<= (+ (* 3 x) (* 4 y)) 12))");
  assert(count(stats, "stronger-bounds") == 0);
  // Negative coefficients need upper endpoints of the other variables.
  stats = diagnose(
      "(assert (>= x 0))(assert (>= y 0))"
      "(assert (= (- (* 3 x) (* 4 y)) 12))");
  assert(count(stats, "new-finite-bounds") == 0);
  assert(count(stats, "stronger-bounds") == 1);  // x >= 4
  stats = diagnose(
      "(assert (>= y 0))(assert (<= x (- 4)))"
      "(assert (<= (+ (* 3 x) (* 4 y)) (- 13)))");
  assert(count(stats, "stronger-bounds") == 1);  // floor(-13/3) = -5
  stats = diagnose(
      "(assert (>= y 0))(assert (<= x (- 5)))"
      "(assert (<= (+ (* 3 x) (* 4 y)) (- 13)))");
  assert(count(stats, "stronger-bounds") == 0);
}

void polarity_strictness_and_real_endpoints() {
  auto stats = diagnose(
      "(assert (not (< y 0)))(assert (<= x 4))"
      "(assert (< (+ x y) 4))");
  assert(count(stats, "stronger-bounds") == 1);  // integer x <= 3
  stats = diagnose(
      "(assert (not (<= y 0)))(assert (<= x 3))"
      "(assert (<= (+ x y) 4))");
  assert(count(stats, "stronger-bounds") == 0);  // integer y >= 1
  stats = diagnose(
      "(assert (>= y 0))(assert (<= x 4))"
      "(assert (< (+ x y) 4))",
      "QF_LRA", "Real");
  assert(count(stats, "stronger-bounds") == 1);  // x < 4
  assert(count(stats, "integer-roundings") == 0);
  stats = diagnose(
      "(assert (> y 0))(assert (< x 4))"
      "(assert (<= (+ x y) 4))",
      "QF_LRA", "Real");
  assert(count(stats, "stronger-bounds") == 0);
  stats = diagnose(
      "(assert (> y 0))(assert (< x 4))"
      "(assert (<= (+ x (* 2 y)) 4))",
      "QF_LRA", "Real");
  assert(count(stats, "stronger-bounds") == 0);
}

void excludes_guards_and_missing_premises() {
  auto stats = diagnose("(assert (or p (>= y 0)))(assert (<= (+ x y) 12))");
  assert(count(stats, "stronger-bounds") == 0);
  assert(count(stats, "missing-premises") == 2);
  stats = diagnose("(assert (>= y 0))(assert (or p (= (+ x y) 12)))");
  assert(count(stats, "compound-rows") == 0);
  stats = diagnose("(assert (>= y 0))(assert (not (= (+ x y) 12)))");
  assert(count(stats, "compound-rows") == 0);
}

void one_pass_and_distinct_endpoints() {
  auto stats = diagnose(
      "(assert (>= y 0))(assert (= (+ x y) 12))"
      "(assert (= (+ x y) 12))(assert (= (+ x z) 20))");
  assert(count(stats, "stronger-bounds") == 1);
  assert(count(stats, "improved-variables") == 1);
  assert(count(stats, "compound-rows") == 2);
  // Equivalent rows have distinct syntax, but still one improved endpoint.
  stats = diagnose(
      "(assert (= (+ (* 2 x) (* 2 y)) 24))"
      "(assert (= (+ x y) 12))(assert (>= y 0))");
  assert(count(stats, "stronger-bounds") == 1);
  assert(count(stats, "rows-with-improvements") == 2);
}

void unsupported_extended_terms_and_uf_leaves() {
  auto stats = diagnose("(assert (>= y 0))(assert (<= (+ x (div y 3)) 12))");
  assert(count(stats, "unsupported-atoms") == 1);
  assert(count(stats, "stronger-bounds") == 0);
  stats = diagnose(
      "(declare-fun f (Int) Int)(assert (>= (f y) 0))"
      "(assert (= (+ x (f y)) 12))",
      "QF_UFLIA");
  assert(count(stats, "stronger-bounds") == 1);
}

void width_cap_and_cancellation() {
  primo::smt::Frontend frontend;
  std::vector<primo::core::TermId> assertions;
  std::string input = "(set-logic QF_LIA)";
  std::string sum = "(+";
  for (std::size_t i = 0; i < 65; ++i) {
    const auto name = "v" + std::to_string(i);
    input += "(declare-const " + name + " Int)(assert (>= " + name + " 0))";
    sum += " " + name;
  }
  input += "(assert (<= " + sum + ") 12))";
  for (const auto& command : primo::smtlib::Parser(input).parse_commands()) {
    frontend.process_command(command, assertions);
  }
  primo::util::Statistics statistics;
  primo::smt::record_root_bound_diagnostics(frontend.terms(), assertions, statistics);
  assert(count(statistics, "wide-rows-skipped") == 1);
  assert(count(statistics, "candidate-directions") == 0);
  primo::smt::record_root_bound_diagnostics(frontend.terms(), assertions, statistics,
                                            [] { return true; });
  assert(count(statistics, "cancelled") == 1);
  assert(count(statistics, "atoms") == 0);
}

void session_scopes_and_disabled_default() {
  primo::smt::CheckSatOptions options;
  options.preprocess_only = true;
  options.arithmetic_bound_diagnostics = true;
  primo::smt::SmtSession session(options);
  for (const auto text : {"(set-logic QF_LIA)", "(declare-const x Int)", "(declare-const y Int)",
                          "(assert (= (+ x y) 12))", "(push 1)", "(assert (>= y 0))"}) {
    assert(session.execute(text).kind == primo::smt::ResponseKind::Success);
  }
  auto response = session.execute("(check-sat)");
  assert(response.outcome->result == primo::smt::CheckSatResult::Unknown);
  assert(count(response.outcome->statistics, "stronger-bounds") == 1);
  assert(!response.outcome->statistics.contains(":sat.conflicts"));
  assert(session.execute("(pop 1)").kind == primo::smt::ResponseKind::Success);
  response = session.execute("(check-sat)");
  assert(count(response.outcome->statistics, "stronger-bounds") == 0);
  options.arithmetic_bound_diagnostics = false;
  const auto disabled = primo::smt::check_sat_smtlib("(set-logic QF_LIA)(check-sat)", options);
  assert(!disabled.statistics.contains(":arithmetic.root-bounds.atoms"));
  options.arithmetic_bound_diagnostics = true;
  options.preprocess_only = false;
  // This query must remain UNSAT with diagnostics enabled.
  const auto unsat = primo::smt::check_sat_smtlib(
      "(set-logic QF_LIA)(declare-const x Int)(assert (< x 0))(assert (>= x 0))(check-sat)",
      options);
  assert(unsat.result == primo::smt::CheckSatResult::Unsat);
  options.incremental_mode = primo::smt::IncrementalMode::Assumptions;
  primo::smt::SmtSession assumptions(options);
  assert(assumptions.execute("(set-logic QF_LRA)").kind == primo::smt::ResponseKind::Error);
}

}  // namespace

int main() {
  nonnegative_equation_and_rounding();
  signed_coefficients_and_negative_floor();
  polarity_strictness_and_real_endpoints();
  excludes_guards_and_missing_premises();
  one_pass_and_distinct_endpoints();
  unsupported_extended_terms_and_uf_leaves();
  width_cap_and_cancellation();
  session_scopes_and_disabled_default();
}
