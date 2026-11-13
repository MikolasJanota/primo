#ifdef NDEBUG
#undef NDEBUG
#endif

#include <cassert>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "core/ids.hpp"
#include "core/symbol_table.hpp"
#include "core/term.hpp"
#include "core/term_manager.hpp"
#include "smt/check_sat.hpp"
#include "smt/frontend.hpp"
#include "smt/preprocess_bound_tightening.hpp"
#include "smt/smt_session.hpp"
#include "smtlib/parser.hpp"
#include "util/rational.hpp"

namespace {
using primo::smt::PreprocessBoundResult;
using primo::util::Rational;

struct Fixture {
  primo::smt::Frontend frontend;
  std::vector<primo::core::TermId> assertions;
  explicit Fixture(std::string_view body, std::string_view sort = "Int",
                   std::string_view logic = "QF_LIA") {
    const auto input = "(set-logic " + std::string(logic) + ")(declare-const x " +
                       std::string(sort) + ")(declare-const y " + std::string(sort) +
                       ")(declare-const z " + std::string(sort) + ")(declare-const p Bool)" +
                       std::string(body);
    for (const auto& command : primo::smtlib::Parser(input).parse_commands()) {
      frontend.process_command(command, assertions);
    }
  }
  PreprocessBoundResult analyze(std::size_t limit = 100000) {
    const auto size = frontend.terms().size();
    auto result = primo::smt::analyze_preprocessing_bounds(frontend.terms(), assertions, limit);
    assert(size == frontend.terms().size());
    return result;
  }
  bool has(const PreprocessBoundResult& result, std::string_view name, Rational value, bool upper,
           bool strict = false) {
    for (const auto& bound : result.bounds) {
      const auto& node = frontend.terms().get(bound.term);
      if (node.kind == primo::core::TermKind::Constant &&
          frontend.terms().symbols().get_symbol(node.symbol).name == name && bound.value == value &&
          bound.upper == upper && bound.strict == strict)
        return true;
    }
    return false;
  }
};

void interval_rules() {
  Fixture nonnegative(
      "(assert (>= x 0))(assert (>= y 0))"
      "(assert (= (+ (* 3 x) (* 4 y)) 13))");
  auto result = nonnegative.analyze();
  assert(nonnegative.has(result, "x", Rational(3), true));
  assert(nonnegative.has(result, "y", Rational(1), true));
  assert(nonnegative.has(result, "x", Rational(3), false));
  assert(nonnegative.has(result, "y", Rational(1), false));
  assert(!result.exhausted && !result.contradiction);
  Fixture negative("(assert (>= y 0))(assert (<= (+ (* 3 x) (* 4 y)) (- 13)))");
  result = negative.analyze();
  assert(negative.has(result, "x", Rational(-5), true));
  Fixture signed_row("(assert (>= y (- 2)))(assert (>= (- (* 3 x) (* 4 y)) 12))");
  result = signed_row.analyze();
  assert(signed_row.has(result, "x", Rational(2), false));
  Fixture guarded(
      "(assert (or p (>= y 0)))(assert (<= (+ x y) 3))"
      "(assert (not (= (+ x z) 4)))");
  assert(guarded.analyze().bounds.empty());
  Fixture chain(
      "(assert (<= (+ x y) 10))(assert (>= (- x z) 2))"
      "(assert (>= y 3))");
  result = chain.analyze();
  assert(chain.has(result, "x", Rational(7), true));
  assert(chain.has(result, "z", Rational(5), true));
  assert(result.row_visits > 2);
}

void strictness_and_limits() {
  Fixture strict("(assert (> y 0))(assert (<= (+ (* 2 x) y) 4))", "Real", "QF_LRA");
  const auto result = strict.analyze();
  assert(strict.has(result, "x", Rational(2), true, true));
  assert(!result.exhausted);
  Fixture equivalent(
      "(assert (< x 4))(assert (> y 0))"
      "(assert (<= (+ x (* 2 y)) 4))",
      "Real", "QF_LRA");
  assert(equivalent.analyze().bounds.empty());
  Fixture approaching(
      "(assert (>= x (- 1)))(assert (>= y (- 1)))"
      "(assert (>= x (* (/ 1 2) y)))(assert (>= y (* (/ 1 2) x)))",
      "Real", "QF_LRA");
  auto partial = approaching.analyze(500);
  assert(partial.exhausted && partial.work == 500 && !partial.bounds.empty());
  assert(!partial.contradiction);
  const auto again = approaching.analyze(500);
  assert(again.work == partial.work && again.bounds.size() == partial.bounds.size());
  for (std::size_t i = 0; i < again.bounds.size(); ++i) {
    assert(again.bounds[i].term == partial.bounds[i].term);
    assert(again.bounds[i].value == partial.bounds[i].value);
  }
  assert(approaching.analyze(0).bounds.empty());
  auto cancelled = primo::smt::analyze_preprocessing_bounds(
      approaching.frontend.terms(), approaching.assertions, 10000, [] { return true; });
  assert(cancelled.cancelled && cancelled.work == 0);
  Fixture contradiction("(assert (>= x 1))(assert (<= y 0))(assert (<= (- x y) 0))");
  assert(contradiction.analyze().contradiction);
  Fixture strict_cross("(assert (> x 0))(assert (<= x 0))", "Real", "QF_LRA");
  assert(strict_cross.analyze().contradiction);
}

void emission_and_wide_rows() {
  Fixture fixture("");
  auto& terms = fixture.frontend.terms();
  const auto sort = terms.symbols().int_sort();
  const auto zero = terms.mk_rational(Rational(0), sort);
  const auto one = terms.mk_rational(Rational(1), sort);
  std::vector<primo::core::TermId> wide;
  for (std::size_t i = 0; i < 4200; ++i) {
    const auto var = fixture.frontend.fresh_internal_constant(sort, "v");
    const auto lower = terms.mk_le(zero, var);
    fixture.assertions.push_back(lower);
    if (i < 65) wide.push_back(var);
    const auto sum = terms.mk_plus({var, fixture.frontend.fresh_internal_constant(sort, "other")});
    // The other term is fixed at zero, so the row derives var <= 1.
    const auto other = terms.get(sum).children;
    // Sum normalization may reorder children; choose the one different from var.
    const auto partner = other[0] == var ? other[1] : other[0];
    fixture.assertions.push_back(terms.mk_eq(partner, zero));
    fixture.assertions.push_back(terms.mk_le(sum, one));
  }
  const auto wide_sum = terms.mk_plus(wide);
  fixture.assertions.push_back(terms.mk_le(wide_sum, one));
  const auto result = fixture.analyze(2000000);
  assert(!result.exhausted && !result.contradiction);
  assert(result.bounds.size() == 4096 && result.emission_truncated == 104);
  // Emission grows the arena while earlier endpoint IDs remain valid.
  const auto extra = primo::smt::materialize_preprocessing_bounds(terms, result);
  assert(extra.size() == 4096);
  for (const auto id : extra) assert(terms.get(id).kind == primo::core::TermKind::Le);
}

void integration() {
  using namespace primo::smt;
  CheckSatOptions options;
  options.preprocess_bound_tightening = true;
  options.produce_model = true;
  options.check_model = true;
  options.preprocessing_mode = PreprocessingMode::Off;
  for (const auto logic : {"QF_LIA", "QF_UFLIA", "QF_LRA", "QF_UFLRA"}) {
    const std::string sort = std::string(logic).find("LIA") != std::string::npos ? "Int" : "Real";
    const auto input = "(set-logic " + std::string(logic) + ")(declare-const x " + sort +
                       ")(declare-const y " + sort +
                       ")(assert (>= x 0))(assert (>= y 0))"
                       "(assert (= (+ (* 3 x) (* 4 y)) 12))(check-sat)";
    const auto tightened = check_sat_smtlib(input, options);
    auto off = options;
    off.preprocess_bound_tightening = false;
    const auto baseline = check_sat_smtlib(input, off);
    assert(tightened.result == CheckSatResult::Sat && baseline.result == tightened.result);
    assert(tightened.statistics.get(":preprocessing.bounds.emitted") >= 2);
    assert(tightened.statistics.get(":lra.rows") <= baseline.statistics.get(":lra.rows"));
    assert(tightened.statistics.get(":lra.tableau-vars") <=
           baseline.statistics.get(":lra.tableau-vars"));
  }
  options.produce_model = false;
  const auto pure_uf = check_sat_smtlib("(set-logic QF_UF)(assert false)(check-sat)", options);
  assert(pure_uf.result == CheckSatResult::Unsat);
  assert(!pure_uf.statistics.contains(":preprocessing.bounds.work"));
  options.preprocess_only = true;
  SmtSession session(options);
  for (const auto text : {"(set-logic QF_LIA)", "(declare-const x Int)", "(declare-const y Int)",
                          "(assert (<= (+ x y) 12))", "(push 1)", "(assert (>= y 0))"}) {
    assert(session.execute(text).kind == ResponseKind::Success);
  }
  auto response = session.execute("(check-sat)");
  assert(response.outcome->statistics.get(":preprocessing.bounds.emitted") == 1);
  assert(session.execute("(pop 1)").kind == ResponseKind::Success);
  response = session.execute("(check-sat)");
  assert(response.outcome->statistics.get(":preprocessing.bounds.emitted") == 0);
  options.preprocess_only = false;
  const auto lowered = check_sat_smtlib(
      "(set-logic QF_LIA)(declare-const x Int)(assert (>= x 0))"
      "(assert (<= (div x 3) 2))(assert (>= x 9))(check-sat)",
      options);
  assert(lowered.result == CheckSatResult::Unsat);
  options.incremental_mode = IncrementalMode::Assumptions;
  SmtSession assumptions(options);
  assert(assumptions.execute("(set-logic QF_LRA)").kind == ResponseKind::Error);
}
}  // namespace

int main() {
  interval_rules();
  strictness_and_limits();
  emission_and_wide_rows();
  integration();
}
