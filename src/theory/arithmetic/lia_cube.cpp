#include "theory/arithmetic/lia_cube.hpp"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

#include "sat/lit.hpp"
#include "theory/arithmetic/lia_cube_mode.hpp"
#include "theory/arithmetic/lia_integer_values.hpp"
#include "theory/arithmetic/lra_tableau.hpp"
#include "util/rational.hpp"

namespace primo::theory::arithmetic {
namespace {
using util::Rational;
using Status = LiaCubeResult::Status;

struct Inequality {
  LiaCubeProblem::Definition terms;
  Rational rhs;
};

Rational abs(const Rational& value) { return value.sign() < 0 ? -value : value; }

bool within_bounds(const LraTableau& tableau, LraVarId var, const DeltaRational& value) {
  const auto lower = tableau.lower_bound(var);
  const auto upper = tableau.upper_bound(var);
  return (!lower || lower->value <= value) && (!upper || value <= upper->value);
}
}  // namespace

LiaCubeProblem LiaCubeProblem::from_tableau(const LraTableau& base,
                                            std::vector<LraVarId> coordinates) {
  LiaCubeProblem problem;
  std::ranges::sort(coordinates);
  coordinates.erase(std::unique(coordinates.begin(), coordinates.end()), coordinates.end());
  problem.coordinates = std::move(coordinates);
  problem.definitions.resize(base.variable_count());
  problem.basic.resize(base.variable_count());
  std::vector<bool> semantic(base.variable_count(), false);
  for (const auto var : problem.coordinates) {
    if (var >= base.variable_count() || base.is_basic(var))
      throw std::logic_error("cube snapshot requires original nonbasic integer coordinates");
    semantic[var] = true;
  }
  for (LraVarId var = 0; var < base.variable_count(); ++var) {
    problem.basic[var] = base.is_basic(var);
    if (!base.is_basic(var)) {
      if (!semantic[var]) throw std::logic_error("cube snapshot has an unclassified coordinate");
      problem.definitions[var].emplace_back(var, Rational(1));
      continue;
    }
    for (const auto& [coordinate, coefficient, slot] : base.row(var)) {
      if (!semantic[coordinate]) throw std::logic_error("cube snapshot has a nonsemantic row term");
      problem.definitions[var].emplace_back(coordinate, coefficient);
    }
  }
  return problem;
}

LiaCubeResult try_cube_model(LraTableau& tableau, const LiaCubeProblem& problem,
                             const std::vector<LraVarId>& integer_vars, LiaCubeMode mode,
                             std::size_t max_steps) {
  LiaCubeResult result;
  if (mode == LiaCubeMode::Off || std::ranges::all_of(integer_vars, [&](LraVarId var) {
        return is_integer_valued(tableau.assignment(var));
      }))
    return result;
  if (problem.definitions.size() != tableau.variable_count())
    throw std::logic_error("cube snapshot no longer matches working tableau");

  std::vector<std::optional<Rational>> fixed(tableau.variable_count());
  for (const auto var : problem.coordinates) {
    const auto lower = tableau.lower_bound(var);
    const auto upper = tableau.upper_bound(var);
    if (lower && upper && lower->value == upper->value) {
      if (!is_integer_valued(lower->value)) return result;
      fixed[var] = lower->value.rational;
      ++result.fixed_coordinates;
    }
  }

  std::vector<Inequality> inequalities;
  bool constant_conflict = false;
  const auto add_bound = [&](LraVarId var, const DeltaRational& bound, bool lower) {
    Inequality inequality;
    DeltaRational rhs = lower ? bound.scaled(Rational(-1)) : bound;
    std::vector<Rational> coefficients;
    for (const auto& [coordinate, coefficient] : problem.definitions[var]) {
      const Rational signed_coefficient = lower ? -coefficient : coefficient;
      if (fixed[coordinate]) {
        rhs.rational -= signed_coefficient * *fixed[coordinate];
      } else {
        inequality.terms.emplace_back(coordinate, signed_coefficient);
        coefficients.push_back(signed_coefficient);
      }
    }
    if (coefficients.empty()) {
      if (rhs < DeltaRational{}) constant_conflict = true;
      return;
    }
    // Positive primitive scaling, followed by delta-aware floor, is equivalent
    // over integral coordinates even for strict or rationally scaled bounds.
    const Rational scale = Rational::primitive_integer_scale(coefficients);
    rhs = rhs.scaled(scale);
    inequality.rhs = delta_floor(rhs);
    Rational norm(0);
    for (auto& [coordinate, coefficient] : inequality.terms) {
      coefficient *= scale;
      norm += abs(coefficient);
    }
    const bool difference = mode == LiaCubeMode::Difference && inequality.terms.size() == 2 &&
                            abs(inequality.terms[0].second) == Rational(1) &&
                            inequality.terms[0].second == -inequality.terms[1].second;
    if (difference)
      ++result.difference_exemptions;
    else
      inequality.rhs -= norm / Rational(2);
    inequalities.push_back(std::move(inequality));
  };
  for (LraVarId var = 0; var < tableau.variable_count(); ++var) {
    if (const auto lower = tableau.lower_bound(var)) add_bound(var, lower->value, true);
    if (const auto upper = tableau.upper_bound(var)) add_bound(var, upper->value, false);
  }
  result.constraints = inequalities.size();
  if (constant_conflict) {
    result.status = Status::Infeasible;
    return result;
  }

  LraTableau center;
  // Preserve original IDs, including unused auxiliary IDs; only coordinates
  // occur in the strengthened rows. Slacks here are fresh rational variables.
  for (LraVarId var = 0; var < tableau.variable_count(); ++var) center.add_variable();
  std::vector<LraVarId> slacks;
  slacks.reserve(inequalities.size());
  for (const auto& inequality : inequalities) {
    const auto slack = center.add_variable();
    center.add_row(slack, inequality.terms);
    slacks.push_back(slack);
  }
  for (std::size_t i = 0; i < inequalities.size(); ++i) {
    if (center.assert_upper(slacks[i], DeltaRational::from_rational(inequalities[i].rhs),
                            sat::Lit{0})) {
      result.status = Status::Infeasible;
      return result;
    }
  }
  const auto checked = center.check_bounded(max_steps);
  result.pivots = center.pivot_count();
  if (checked != LraTableau::BoundedCheckStatus::Feasible) {
    result.status = checked == LraTableau::BoundedCheckStatus::LimitReached ? Status::LimitReached
                                                                            : Status::Infeasible;
    return result;
  }
  result.center_found = true;
  std::vector<Rational> rounded(tableau.variable_count());
  for (const auto var : problem.coordinates) {
    // A single translation-invariant tie rule is needed for difference rows.
    rounded[var] = fixed[var].value_or((center.assignment(var).rational + Rational(1, 2)).floor());
  }
  std::vector<DeltaRational> values(tableau.variable_count());
  for (LraVarId var = 0; var < tableau.variable_count(); ++var) {
    Rational value(0);
    for (const auto& [coordinate, coefficient] : problem.definitions[var])
      value += coefficient * rounded[coordinate];
    values[var] = DeltaRational::from_rational(value);
    if (!within_bounds(tableau, var, values[var])) {
      result.status = Status::CandidateRejected;
      return result;
    }
  }
  if (!std::ranges::all_of(integer_vars,
                           [&](LraVarId var) { return is_integer_valued(values[var]); })) {
    result.status = Status::CandidateRejected;
    return result;
  }

  // Reconstruct the original basis without fixing bounds. No cube constraints
  // or speculative reasons survive adoption. Existing reason IDs are retained.
  LraTableau candidate;
  for (LraVarId var = 0; var < tableau.variable_count(); ++var) candidate.add_variable();
  for (LraVarId var = 0; var < tableau.variable_count(); ++var)
    if (problem.basic[var]) candidate.add_row(var, problem.definitions[var]);
  for (const auto var : problem.coordinates) candidate.set_nonbasic_value(var, values[var]);
  for (LraVarId var = 0; var < tableau.variable_count(); ++var) {
    if (const auto lower = tableau.lower_bound(var)) {
      if (candidate.assert_lower(var, lower->value, lower->reason))
        throw std::logic_error("validated cube model has conflicting lower bound");
    }
    if (const auto upper = tableau.upper_bound(var)) {
      if (candidate.assert_upper(var, upper->value, upper->reason))
        throw std::logic_error("validated cube model has conflicting upper bound");
    }
    if (candidate.assignment(var) != values[var])
      throw std::logic_error("cube model reconstruction changed an assignment");
  }
  if (candidate.check_bounded(0) != LraTableau::BoundedCheckStatus::Feasible)
    throw std::logic_error("cube model reconstruction requires repair");
  tableau = std::move(candidate);
  result.status = Status::Sat;
  return result;
}
}  // namespace primo::theory::arithmetic
