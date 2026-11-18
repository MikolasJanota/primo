#include "smt/root_bound_diagnostics.hpp"

#include <chrono>
#include <cstddef>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "core/ids.hpp"
#include "core/term.hpp"
#include "core/term_manager.hpp"
#include "theory/arithmetic/lra_atom.hpp"
#include "theory/arithmetic/lra_tableau.hpp"
#include "util/rational.hpp"
#include "util/statistics.hpp"

namespace primo::smt {
namespace {

using theory::arithmetic::DeltaRational;
using theory::arithmetic::LraAtom;
using theory::arithmetic::LraRelation;
using util::Rational;

struct Bounds {
  std::optional<DeltaRational> lower;
  std::optional<DeltaRational> upper;
};
using BoundMap = std::unordered_map<core::TermId, Bounds>;

struct Constraint {
  LraAtom atom;
  bool strict;
};

bool tighten(Bounds& bounds, bool upper, const DeltaRational& value) {
  auto& old = upper ? bounds.upper : bounds.lower;
  if (!old || (upper ? value < *old : *old < value)) {
    old = value;
    return true;
  }
  return false;
}

DeltaRational integer_endpoint(const DeltaRational& value, bool upper) {
  Rational rounded = upper ? value.rational.floor() : value.rational.ceil();
  if (value.rational.is_integer()) {
    if (upper && value.infinitesimal.sign() < 0) rounded -= Rational(1);
    if (!upper && value.infinitesimal.sign() > 0) rounded += Rational(1);
  }
  return DeltaRational::from_rational(std::move(rounded));
}

// Extended arithmetic has not been lowered at this boundary. Do not let the
// polynomial normalizer treat it as an unconstrained opaque arithmetic leaf.
bool supported_polynomial(const core::TermManager& terms, core::TermId root,
                          const std::function<bool()>& cancelled) {
  std::vector<core::TermId> pending{root};
  std::unordered_set<core::TermId> seen;
  while (!pending.empty()) {
    if (cancelled && cancelled()) return false;
    const auto id = pending.back();
    pending.pop_back();
    if (!seen.insert(id).second) continue;
    const auto& node = terms.get(id);
    switch (node.kind) {
      case core::TermKind::Plus:
      case core::TermKind::Times:
        pending.insert(pending.end(), node.children.begin(), node.children.end());
        break;
      case core::TermKind::Rational:
      case core::TermKind::Constant:
      case core::TermKind::Application:
        break;
      default:
        return false;
    }
  }
  return true;
}

}  // namespace

void record_root_bound_diagnostics(const core::TermManager& terms,
                                   std::span<const core::TermId> assertions,
                                   util::Statistics& statistics,
                                   const std::function<bool()>& cancelled) {
  const auto start = std::chrono::steady_clock::now();
  const auto increment = [&statistics](util::StatKey key, std::size_t value = 1) {
    statistics.increment(key, value);
  };
  for (const char* key :
       {"atoms", "supported-atoms", "unsupported-atoms", "ignored-formulas", "compound-rows",
        "wide-rows-skipped", "explicit-bounds", "variables", "candidate-directions",
        "missing-premises", "rows-with-improvements", "stronger-bounds", "new-finite-bounds",
        "improved-variables", "integer-roundings", "cancelled", "time-us"}) {
    statistics.set_dynamic(std::string(":arithmetic.root-bounds.") + key, 0);
  }
  const auto interrupted = [&] {
    if (!cancelled || !cancelled()) return false;
    statistics.set_dynamic(":arithmetic.root-bounds.cancelled", 1);
    return true;
  };
  BoundMap explicit_bounds;
  std::vector<Constraint> rows;
  std::unordered_map<core::TermId, unsigned> visited;
  std::vector<std::pair<core::TermId, bool>> pending;
  for (const auto id : assertions) pending.emplace_back(id, false);
  while (!pending.empty() && !interrupted()) {
    const auto [id, negative] = pending.back();
    pending.pop_back();
    const unsigned polarity = negative ? 2U : 1U;
    if ((visited[id] & polarity) != 0) continue;
    visited[id] |= polarity;
    const auto& node = terms.get(id);
    if (node.kind == core::TermKind::Not) {
      pending.emplace_back(node.children.at(0), !negative);
      continue;
    }
    if (node.kind == core::TermKind::And && !negative) {
      for (const auto child : node.children) pending.emplace_back(child, false);
      continue;
    }
    if ((node.kind != core::TermKind::Equality && node.kind != core::TermKind::Le &&
         node.kind != core::TermKind::Lt) ||
        (negative && node.kind == core::TermKind::Equality) ||
        (terms.get(node.children.at(0)).sort != terms.symbols().int_sort() &&
         terms.get(node.children.at(0)).sort != terms.symbols().real_sort())) {
      increment(util::StatKey::ArithmeticRootBoundsIgnoredFormulas);
      continue;
    }
    increment(util::StatKey::ArithmeticRootBoundsAtoms);
    if (!supported_polynomial(terms, node.children.at(0), cancelled) ||
        !supported_polynomial(terms, node.children.at(1), cancelled)) {
      if (interrupted()) break;
      increment(util::StatKey::ArithmeticRootBoundsUnsupportedAtoms);
      continue;
    }
    auto atom = theory::arithmetic::normalize_atom(terms, id);
    bool strict = atom.relation == LraRelation::Lt;
    if (negative) {
      atom.polynomial.scale(Rational(-1));
      atom.rhs = -atom.rhs;
      strict = !strict;
    }
    increment(util::StatKey::ArithmeticRootBoundsSupportedAtoms);
    const auto size = atom.polynomial.terms().size();
    if (size == 1) {
      // This borrows atom's owned map; endpoint mutates only explicit_bounds.
      const auto& [variable, coefficient] = *atom.polynomial.terms().begin();
      const bool upper = coefficient.sign() > 0;
      const auto endpoint = [&](bool is_upper, bool is_strict) {
        DeltaRational value{atom.rhs / coefficient, Rational(is_strict ? (is_upper ? -1 : 1) : 0)};
        if (terms.get(variable).sort == terms.symbols().int_sort()) {
          value = integer_endpoint(value, is_upper);
        }
        tighten(explicit_bounds[variable], is_upper, value);
      };
      endpoint(upper, strict);
      if (atom.relation == LraRelation::Eq) endpoint(!upper, false);
    } else if (size > 1) {
      increment(util::StatKey::ArithmeticRootBoundsCompoundRows);
      if (size > 64) {
        increment(util::StatKey::ArithmeticRootBoundsWideRowsSkipped);
      } else {
        rows.push_back(Constraint{std::move(atom), strict});
      }
    }
  }
  for (const auto& [variable, bounds] : explicit_bounds) {
    (void)variable;
    increment(util::StatKey::ArithmeticRootBoundsExplicitBounds,
              static_cast<std::size_t>(bounds.lower.has_value()) +
                  static_cast<std::size_t>(bounds.upper.has_value()));
  }
  BoundMap candidates = explicit_bounds;
  std::unordered_set<core::TermId> variables;
  for (const auto& row : rows) {
    if (interrupted()) break;
    bool improves = false;
    for (const auto& [target, coefficient] : row.atom.polynomial.terms()) {
      if (interrupted()) break;
      variables.insert(target);
      const int directions = row.atom.relation == LraRelation::Eq ? 2 : 1;
      for (int direction = 0; direction < directions; ++direction) {
        // CAV 2006 §4.5 interval refinement, applied to an original input
        // constraint. Equality supplies both <= directions. Candidate bounds
        // never become premises, so this is not fixed-point propagation.
        increment(util::StatKey::ArithmeticRootBoundsCandidateDirections);
        const Rational sign(direction == 0 ? 1 : -1);
        DeltaRational remainder{row.atom.rhs * sign, Rational(row.strict ? -1 : 0)};
        bool available = true;
        for (const auto& [other, other_coefficient] : row.atom.polynomial.terms()) {
          if (other == target) continue;
          const auto found = explicit_bounds.find(other);
          const auto* bound =
              found == explicit_bounds.end()
                  ? nullptr
                  : (other_coefficient.sign() * sign.sign() > 0 ? &found->second.lower
                                                                : &found->second.upper);
          if (!bound || !bound->has_value()) {
            available = false;
            break;
          }
          remainder = remainder - (**bound).scaled(other_coefficient * sign);
        }
        if (!available) {
          increment(util::StatKey::ArithmeticRootBoundsMissingPremises);
          continue;
        }
        const auto signed_coefficient = coefficient * sign;
        const bool upper = signed_coefficient.sign() > 0;
        auto value = remainder.scaled(Rational(1) / signed_coefficient);
        if (terms.get(target).sort == terms.symbols().int_sort()) {
          const auto rounded = integer_endpoint(value, upper);
          if (rounded != value) increment(util::StatKey::ArithmeticRootBoundsIntegerRoundings);
          value = rounded;
        } else {
          // Count semantic Real endpoints, not different positive multiples
          // of delta that all express the same strict rational endpoint.
          value.infinitesimal = Rational(value.infinitesimal.sign());
        }
        const auto found = explicit_bounds.find(target);
        const auto& baseline = found == explicit_bounds.end()
                                   ? std::optional<DeltaRational>{}
                                   : (upper ? found->second.upper : found->second.lower);
        if (!baseline || (upper ? value < *baseline : *baseline < value)) improves = true;
        tighten(candidates[target], upper, value);
      }
    }
    if (improves) increment(util::StatKey::ArithmeticRootBoundsRowsWithImprovements);
  }
  for (const auto& [variable, bounds] : candidates) {
    variables.insert(variable);
    bool improves = false;
    const auto found = explicit_bounds.find(variable);
    for (const bool upper : {false, true}) {
      const auto& candidate = upper ? bounds.upper : bounds.lower;
      const auto& baseline = found == explicit_bounds.end()
                                 ? std::optional<DeltaRational>{}
                                 : (upper ? found->second.upper : found->second.lower);
      if (candidate && (!baseline || (upper ? *candidate < *baseline : *baseline < *candidate))) {
        increment(util::StatKey::ArithmeticRootBoundsStrongerBounds);
        if (!baseline) increment(util::StatKey::ArithmeticRootBoundsNewFiniteBounds);
        improves = true;
      }
    }
    if (improves) increment(util::StatKey::ArithmeticRootBoundsImprovedVariables);
  }
  increment(util::StatKey::ArithmeticRootBoundsVariables, variables.size());
  const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
                           std::chrono::steady_clock::now() - start)
                           .count();
  statistics.set_dynamic(":arithmetic.root-bounds.time-us",
                         static_cast<util::Statistics::Value>(elapsed));
}

}  // namespace primo::smt
