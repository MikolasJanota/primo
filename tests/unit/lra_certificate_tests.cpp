#ifdef NDEBUG
#undef NDEBUG
#endif

#include <unistd.h>

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "sat/lit.hpp"
#include "smt/check_sat.hpp"
#include "smt/numeric_clause_spool.hpp"
#include "theory/arithmetic/lra_certificate.hpp"
#include "theory/arithmetic/lra_tableau.hpp"
#include "util/rational.hpp"

namespace {
using primo::theory::arithmetic::check_lra_certificate;
using primo::theory::arithmetic::LraCertificatePremise;
using primo::theory::arithmetic::LraCertificateStatus;
using primo::theory::arithmetic::reconstruct_lra_certificate;
using primo::util::Rational;

void arithmetic() {
  // x+y<=2, -x<=-2, -y<0: a strict boundary contradiction.
  const std::vector<LraCertificatePremise> strict{
      {{{0, Rational(1)}, {1, Rational(1)}}, Rational(2), false},
      {{{0, Rational(-1)}}, Rational(-2), false},
      {{{1, Rational(-1)}}, Rational(0), true}};
  auto result = reconstruct_lra_certificate(strict);
  assert(result.status == LraCertificateStatus::Verified);
  assert(check_lra_certificate(strict, result.weights));
  assert(result.weights.size() == strict.size() && result.pivots > 0);
  assert(!check_lra_certificate(strict, std::vector<Rational>{1, 1, 0}));
  assert(!check_lra_certificate(strict, std::vector<Rational>{-1, -1, -1}));
  const std::vector<LraCertificatePremise> weak{
      {{{0, Rational(2)}, {1, Rational(1)}}, Rational(3), false},
      {{{0, Rational(-1)}}, Rational(-2), false},
      {{{1, Rational(-1)}}, Rational(0), false}};
  result = reconstruct_lra_certificate(weak);
  assert(result.status == LraCertificateStatus::Verified);
  assert(check_lra_certificate(weak, result.weights));
  auto satisfiable = strict;
  satisfiable.back().strict = false;
  assert(reconstruct_lra_certificate(satisfiable).status == LraCertificateStatus::Infeasible);
  assert(reconstruct_lra_certificate({}).status == LraCertificateStatus::Infeasible);
  // A discarded support coefficient must remain zero without losing its premise.
  const std::vector<LraCertificatePremise> constants{{{}, Rational(-1), false},
                                                     {{{19, Rational(1)}}, Rational(20), true}};
  result = reconstruct_lra_certificate(constants);
  assert(result.status == LraCertificateStatus::Verified);
  assert(result.weights.size() == 2 && result.weights[1].is_zero());
  const std::vector<LraCertificatePremise> fraction{
      {{{3, Rational(1, 7)}, {3, Rational(2, 7)}}, Rational(0), true},
      {{{3, Rational(-1)}}, Rational(0), false}};
  result = reconstruct_lra_certificate(fraction);
  assert(result.status == LraCertificateStatus::Verified);
  assert(check_lra_certificate(fraction, result.weights));
  assert(reconstruct_lra_certificate(strict, [] { return true; }).status ==
         LraCertificateStatus::Interrupted);
}

void interrupt_inside_check() {
  using namespace primo::theory::arithmetic;
  LraTableau tableau;
  const auto x = tableau.add_variable();
  const auto y = tableau.add_variable();
  const auto sum = tableau.add_variable();
  tableau.add_row(sum, {{x, Rational(1)}, {y, Rational(1)}});
  assert(!tableau.assert_lower(sum, DeltaRational::from_rational(Rational(1)), primo::sat::Lit{1}));
  std::size_t polls = 0;
  bool interrupted = false;
  try {
    static_cast<void>(tableau.check([&] { return ++polls == 2; }));
  } catch (const LraCheckInterrupted&) {
    interrupted = true;
  }
  assert(interrupted && tableau.pivot_count() > 0);
}

void numeric_records() {
  for (const bool indexed : {false, true}) {
    primo::smt::NumericClauseSpool spool(indexed);
    spool.append({primo::sat::Lit{4}, primo::sat::Lit{-7}});
    spool.append({});
    assert(spool.size() == 2);
    if (indexed) assert(spool.read(1).empty());
    const auto first = spool.read(0);
    assert(first.size() == 2 && first[0].value == 4 && first[1].value == -7);
    assert(spool.read(1).empty());
  }
}

void cancelled_export_preserves_output() {
  using namespace primo::smt;
  const std::string input =
      "(set-logic QF_LRA)(declare-fun x () Real)(declare-fun y () Real)"
      "(assert (<= (+ x y) 2))(assert (>= x 2))(assert (> y 0))(check-sat)";
  const auto path = std::filesystem::temp_directory_path() /
                    ("primo-lra-certificate-unit-" + std::to_string(::getpid()) + ".sexp");
  CheckSatOptions options;
  options.export_cnf = path.string();
  options.preprocessing_mode = PreprocessingMode::Off;
  std::size_t polls = 0;
  options.cancelled = [&] {
    ++polls;
    return false;
  };
  const auto complete = check_sat_smtlib(input, options);
  assert(complete.result == CheckSatResult::Unsat && polls > 3);
  const auto cutoff = polls - 1;
  {
    std::ofstream output(path);
    output << "preserve";
  }
  polls = 0;
  options.cancelled = [&] { return ++polls >= cutoff; };
  const auto cancelled = check_sat_smtlib(input, options);
  assert(cancelled.result == CheckSatResult::Unknown);
  assert(cancelled.statistics.get(":export.main-unsat") == 1);
  std::ifstream stream(path);
  const std::string contents{std::istreambuf_iterator<char>(stream),
                             std::istreambuf_iterator<char>()};
  assert(contents == "preserve");
  std::filesystem::remove(path);
}
}  // namespace

int main() {
  arithmetic();
  interrupt_inside_check();
  numeric_records();
  cancelled_export_preserves_output();
}
