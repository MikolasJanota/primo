#ifdef NDEBUG
#undef NDEBUG
#endif

#include <unistd.h>

#include <cassert>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "smt/check_sat.hpp"
#include "theory/arithmetic/lia_certificate.hpp"
#include "util/rational.hpp"

namespace {
using namespace primo::theory::arithmetic;
using primo::util::Rational;

void arithmetic() {
  const std::vector<LiaCertificatePremise> example{
      {{{0, 3}, {1, 2}}, 4}, {{{1, -1}}, -1}, {{{0, -2}}, -1}};
  auto proof = reconstruct_lia_certificate(example);
  assert(proof.status == LiaCertificateStatus::Verified);
  assert(check_lia_certificate(example, proof));
  assert(proof.generated_steps > 0 && proof.linear_checks == 1);
  assert(proof.projections == 0);
  // Pugh's exact-projection preference matters: eliminating x first loses
  // the half-integral obstruction, while unit-coefficient y exposes it.
  const std::vector<LiaCertificatePremise> projection{
      {{{0, 2}, {1, 1}}, 2}, {{{0, -2}, {1, 1}}, 0}, {{{1, -1}}, -1}};
  proof = reconstruct_lia_certificate(projection);
  assert(proof.status == LiaCertificateStatus::Verified &&
         check_lia_certificate(projection, proof));
  assert(proof.pairs > 0 && proof.exact_projections > 0);
  // The second cut must floor -1/2 to -1, never truncate it to zero.
  const std::vector<LiaCertificatePremise> half{{{{0, 2}}, 1}, {{{0, -2}}, -1}};
  proof = reconstruct_lia_certificate(half);
  assert(proof.status == LiaCertificateStatus::Verified && check_lia_certificate(half, proof));
  auto invalid = proof;
  invalid.cuts.front().divisor = Rational(3);
  assert(!check_lia_certificate(half, invalid));
  invalid = proof;
  invalid.cuts.front().antecedents.front().second = Rational(-1);
  assert(!check_lia_certificate(half, invalid));
  invalid = proof;
  invalid.cuts.front().antecedents.front().first = half.size();
  assert(!check_lia_certificate(half, invalid));
  invalid = proof;
  invalid.cuts.front().divisor = Rational(0);
  assert(!check_lia_certificate(half, invalid));
  const std::vector<LiaCertificatePremise> sat{{{{0, 1}}, 1}, {{{0, -1}}, 0}};
  assert(reconstruct_lia_certificate(sat).status == LiaCertificateStatus::NotFound);
  const std::vector<LiaCertificatePremise> duplicates{{{{0, 1}, {0, 1}}, 1}, {{{0, -2}}, -1}};
  proof = reconstruct_lia_certificate(duplicates);
  assert(proof.status == LiaCertificateStatus::Verified &&
         check_lia_certificate(duplicates, proof));
  // Original premises remain; unused intermediate normalization steps are pruned.
  const std::vector<LiaCertificatePremise> constant{{{}, -1}, {{{3, 4}}, 100}};
  proof = reconstruct_lia_certificate(constant);
  assert(proof.status == LiaCertificateStatus::Verified && proof.cuts.empty());
  assert(proof.pruned_steps == 1 && proof.contradiction == 0);
  assert(check_lia_certificate(constant, proof));
  proof.cuts.push_back({Rational(1), {{0, Rational(1)}, {1, Rational(0)}}});
  proof.contradiction = 2;
  assert(check_lia_certificate(constant, proof));
  LiaCertificateLimits limits;
  limits.steps = 0;
  assert(reconstruct_lia_certificate(half, {}, limits).status ==
         LiaCertificateStatus::ResourceLimit);
  limits = {};
  limits.pairs = 0;
  assert(reconstruct_lia_certificate(projection, {}, limits).status ==
         LiaCertificateStatus::ResourceLimit);
  assert(reconstruct_lia_certificate(example, [] { return true; }).status ==
         LiaCertificateStatus::Interrupted);
  std::size_t polls = 0;
  assert(reconstruct_lia_certificate(example, [&] { return ++polls > 30; }).status ==
         LiaCertificateStatus::Interrupted);
  // Pugh §2.3.3: its real shadow has integer points requiring additional reasoning.
  const std::vector<LiaCertificatePremise> nightmare{{{{0, 11}, {1, 13}}, 45},
                                                     {{{0, -11}, {1, -13}}, -27},
                                                     {{{0, 7}, {1, -9}}, 4},
                                                     {{{0, -7}, {1, 9}}, 10}};
  assert(reconstruct_lia_certificate(nightmare).status == LiaCertificateStatus::NotFound);
}

void exporter() {
  using namespace primo::smt;
  const std::string input =
      "(set-logic QF_LIA)(declare-fun x () Int)(declare-fun y () Int)"
      "(assert (<= (+ (* 3 x) (* 2 y)) 4))(assert (<= 1 y))(assert (<= 1 (* 2 x)))(check-sat)";
  const auto path = std::filesystem::temp_directory_path() /
                    ("primo-lia-certificate-unit-" + std::to_string(::getpid()) + ".sexp");
  CheckSatOptions options;
  options.export_cnf = path.string();
  options.preprocessing_mode = PreprocessingMode::Off;
  options.export_cnf_reduction = ExportCnfReduction::Core;
  const auto outcome = check_sat_smtlib(input, options);
  assert(outcome.result == CheckSatResult::Unsat);
  assert(outcome.statistics.get(":export.main-unsat") == 1);
  assert(outcome.statistics.get(":export.reconstruction.completed") == 1);
  assert(outcome.statistics.get(":export.reconstruction.lia-generated-cuts") > 0);
  {
    std::ifstream stream(path);
    const std::string text{std::istreambuf_iterator<char>(stream),
                           std::istreambuf_iterator<char>()};
    assert(text.find("(version 3)") != std::string::npos);
    assert(text.find("(conflict-lia") != std::string::npos);
    assert(text.find("(contradiction ") != std::string::npos);
  }
  const std::string incomplete =
      "(set-logic QF_LIA)(declare-fun x () Int)(declare-fun y () Int)"
      "(assert (<= 27 (+ (* 11 x) (* 13 y))))"
      "(assert (<= (+ (* 11 x) (* 13 y)) 45))"
      "(assert (<= (- 10) (- (* 7 x) (* 9 y))))"
      "(assert (<= (- (* 7 x) (* 9 y)) 4))(check-sat)";
  {
    std::ofstream stream(path);
    stream << "preserve";
  }
  const auto stopped = check_sat_smtlib(incomplete, options);
  assert(stopped.result == CheckSatResult::Unknown);
  assert(stopped.statistics.get(":export.main-unsat") == 1);
  {
    std::ifstream stream(path);
    const std::string text{std::istreambuf_iterator<char>(stream),
                           std::istreambuf_iterator<char>()};
    assert(text == "preserve");
  }
  std::filesystem::remove(path);
}
}  // namespace

int main() {
  arithmetic();
  exporter();
}
