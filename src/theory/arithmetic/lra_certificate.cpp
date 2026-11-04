#include "theory/arithmetic/lra_certificate.hpp"

#include <algorithm>
#include <map>
#include <stdexcept>
#include <utility>
#include <vector>

#include "sat/lit.hpp"
#include "theory/arithmetic/lra_tableau.hpp"

namespace primo::theory::arithmetic {

bool check_lra_certificate(std::span<const LraCertificatePremise> premises,
                           std::span<const util::Rational> weights) {
  if (premises.size() != weights.size()) return false;
  std::map<std::size_t, util::Rational> sum;
  util::Rational rhs(0);
  bool strict = false;
  for (std::size_t i = 0; i < premises.size(); ++i) {
    if (weights[i].sign() < 0) return false;
    if (weights[i].sign() == 0) continue;
    rhs = rhs + weights[i] * premises[i].rhs;
    strict = strict || premises[i].strict;
    for (const auto& [variable, coefficient] : premises[i].coefficients) {
      sum[variable] = sum[variable] + weights[i] * coefficient;
    }
  }
  return std::all_of(sum.begin(), sum.end(),
                     [](const auto& term) { return term.second.sign() == 0; }) &&
         (rhs.sign() < 0 || (rhs.sign() == 0 && strict));
}

LraCertificateResult reconstruct_lra_certificate(std::span<const LraCertificatePremise> premises,
                                                 const std::function<bool()>& cancelled) {
  LraCertificateResult result;
  try {
    const auto poll = [&] {
      if (cancelled && cancelled()) throw LraCheckInterrupted();
    };
    poll();
    LraTableau tableau;  // Private Bland feasibility search, no SMT/SAT search.
    std::vector<LraVarId> multipliers;
    multipliers.reserve(premises.size());
    for (std::size_t i = 0; i < premises.size(); ++i) {
      poll();
      multipliers.push_back(tableau.add_variable());
    }
    std::map<std::size_t, std::vector<std::pair<LraVarId, util::Rational>>> columns;
    std::vector<std::pair<LraVarId, util::Rational>> constant_row;
    std::vector<std::pair<LraVarId, util::Rational>> normalization_row;
    for (std::size_t i = 0; i < premises.size(); ++i) {
      poll();
      std::map<std::size_t, util::Rational> combined;
      for (const auto& [variable, coefficient] : premises[i].coefficients) {
        poll();
        combined[variable] += coefficient;
      }
      for (const auto& [variable, coefficient] : combined) {
        if (coefficient.sign() == 0) continue;
        columns[variable].emplace_back(multipliers[i], coefficient);
        ++result.nonzeros;
      }
      if (premises[i].rhs.sign() != 0) constant_row.emplace_back(multipliers[i], premises[i].rhs);
      const auto value = util::Rational(premises[i].strict ? 1 : 0) - premises[i].rhs;
      if (value.sign() != 0) normalization_row.emplace_back(multipliers[i], value);
    }
    result.variables = columns.size();
    std::vector<LraVarId> zero_rows;
    for (const auto& [variable, entries] : columns) {
      static_cast<void>(variable);
      poll();
      const auto row = tableau.add_variable();
      tableau.add_row(row, entries);
      zero_rows.push_back(row);
    }
    const auto constant = tableau.add_variable();
    tableau.add_row(constant, constant_row);
    const auto normalization = tableau.add_variable();
    tableau.add_row(normalization, normalization_row);
    // These private labels only populate unused infeasibility explanations.
    // They never enter the source SAT backend or the exported lemma.
    constexpr sat::Lit reason{1};
    const auto lower = [&](LraVarId variable, int value) {
      poll();
      return tableau
          .assert_lower(variable, DeltaRational::from_rational(util::Rational(value)), reason)
          .has_value();
    };
    const auto upper = [&](LraVarId variable, int value) {
      poll();
      return tableau
          .assert_upper(variable, DeltaRational::from_rational(util::Rational(value)), reason)
          .has_value();
    };
    for (const auto variable : multipliers)
      if (lower(variable, 0)) return result;
    for (const auto variable : zero_rows)
      if (lower(variable, 0) || upper(variable, 0)) return result;
    if (upper(constant, 0) || lower(normalization, 1) || upper(normalization, 1)) return result;
    const auto conflict = tableau.check(cancelled);
    result.pivots = tableau.pivot_count();
    if (conflict.has_value()) return result;
    for (const auto variable : multipliers) {
      poll();
      const auto& value = tableau.assignment(variable);
      if (value.infinitesimal.sign() != 0)
        throw std::logic_error("infinitesimal in non-strict certificate reconstruction");
      result.weights.push_back(value.rational);
    }
    poll();
    if (!check_lra_certificate(premises, result.weights))
      throw std::logic_error("reconstructed LRA witness failed exact checking");
    result.status = LraCertificateStatus::Verified;
  } catch (const LraCheckInterrupted&) {
    result.weights.clear();
    result.status = LraCertificateStatus::Interrupted;
  }
  return result;
}

}  // namespace primo::theory::arithmetic
