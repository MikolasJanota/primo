#include "smt/conflict_ackermann.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <utility>

#include "core/ids.hpp"
#include "core/term.hpp"
#include "sat/clause.hpp"
#include "sat/lit.hpp"
namespace primo::smt {

namespace {
std::uint64_t semantic_pair(core::TermId lhs, core::TermId rhs) {
  if (rhs < lhs) std::swap(lhs, rhs);
  return (static_cast<std::uint64_t>(lhs) << 32U) | rhs;
}
}  // namespace

void AckermannGrowthState::register_atom(core::TermId atom) {
  if (emit_) return;  // Only dry-run queries need a callback-time equality snapshot.
  const auto& node = terms_.get(atom);
  if (node.kind == core::TermKind::Equality)
    known_equalities_.insert(semantic_pair(node.children[0], node.children[1]));
}

void AckermannGrowthState::request(core::TermId lhs, core::TermId rhs) {
  const auto key = semantic_pair(lhs, rhs);
  if (settled_.contains(key)) return;
  if (requests_.size() >= 64 || settled_.size() >= 4096) {
    statistics_.increment(util::StatKey::EufConflictAckDemandRequestLimit);
    return;
  }
  const auto& a = terms_.get(lhs);
  const auto& b = terms_.get(rhs);
  if (lhs == rhs || a.kind != core::TermKind::Application ||
      b.kind != core::TermKind::Application || a.symbol != b.symbol ||
      a.children.size() != b.children.size() || a.children.size() >= 32)
    return;
  Request request{lhs, rhs, {}};
  // Boolean equalities require a different abstraction contract in this milestone.
  if (a.sort == terms_.symbols().bool_sort()) return;
  for (std::size_t i = 0; i < a.children.size(); ++i) {
    if (a.children[i] == b.children[i]) continue;
    if (terms_.get(a.children[i]).sort == terms_.symbols().bool_sort()) {
      statistics_.increment(util::StatKey::EufConflictAckDemandBooleanSkips);
      return;
    }
    request.arguments.emplace_back(a.children[i], b.children[i]);
  }
  settled_.insert(key);
  if (!emit_) {
    std::unordered_set<std::uint64_t> missing;
    if (!known_equalities_.contains(key)) missing.insert(key);
    for (const auto& [x, y] : request.arguments) {
      const auto argument = semantic_pair(x, y);
      if (!known_equalities_.contains(argument)) missing.insert(argument);
    }
    if (atom_limit_ && missing.size() > atom_limit_)
      statistics_.increment(util::StatKey::EufConflictAckDemandBudgetSkips);
    else
      statistics_.increment(util::StatKey::EufConflictAckDemandDryRunInstances);
    statistics_.increment(util::StatKey::EufConflictAckDemandDryRunMissingAtoms, missing.size());
    return;
  }
  requests_.push_back(std::move(request));
  statistics_.increment(util::StatKey::EufConflictAckDemandRequests);
}

void AckermannGrowthState::drain(BooleanClausifier& clausifier,
                                 const std::function<void(core::TermId, core::TermId)>& bridge) {
  // No borrowed term or registry reference survives a constructor/registration.
  std::unordered_map<std::uint64_t, core::TermId> registered;
  for (const auto& atom : clausifier.theory_atoms()) {
    const auto& node = terms_.get(atom.term);
    if (node.kind == core::TermKind::Equality)
      registered.try_emplace(semantic_pair(node.children[0], node.children[1]), atom.term);
  }
  const auto equality_for = [&](core::TermId lhs, core::TermId rhs) {
    const auto found = registered.find(semantic_pair(lhs, rhs));
    if (found != registered.end()) return found->second;
    return terms_.mk_eq(std::min(lhs, rhs), std::max(lhs, rhs));
  };
  for (const auto& request : requests_) {
    std::vector<core::TermId> equalities;
    equalities.reserve(request.arguments.size() + 1);
    equalities.push_back(equality_for(request.lhs, request.rhs));
    for (const auto& [lhs, rhs] : request.arguments) equalities.push_back(equality_for(lhs, rhs));
    std::unordered_set<core::TermId> missing;
    for (const auto equality : equalities) {
      const auto children = terms_.get(equality).children;
      if (!registered.contains(semantic_pair(children[0], children[1]))) missing.insert(equality);
    }
    statistics_.increment(util::StatKey::EufConflictAckDemandReusedAtoms,
                          equalities.size() - missing.size());
    if (atom_limit_ && missing.size() > atom_limit_ - fresh_atoms_) {
      statistics_.increment(util::StatKey::EufConflictAckDemandBudgetSkips);
      continue;
    }
    // Preflight above covers the entire instance. Bridges add encodings, not
    // additional equality atoms charged to this budget.
    for (const auto equality : equalities) {
      const auto children = terms_.get(equality).children;
      if (bridge && missing.contains(equality)) bridge(children[0], children[1]);
      static_cast<void>(clausifier.ensure_theory_atom(equality));
      registered.try_emplace(semantic_pair(children[0], children[1]), equality);
    }
    fresh_atoms_ += missing.size();
    statistics_.increment(util::StatKey::EufConflictAckDemandFreshAtoms, missing.size());
    statistics_.set(util::StatKey::EufConflictAckDemandPeakFreshAtoms, fresh_atoms_);
    std::vector<core::TermId> disjunction{equalities.front()};
    for (std::size_t i = 1; i < equalities.size(); ++i)
      disjunction.push_back(terms_.mk_not(equalities[i]));
    clausifier.assert_formula(terms_.mk_or(std::move(disjunction)));
    statistics_.increment(util::StatKey::EufConflictAckDemandInstalledInstances);
  }
  requests_.clear();
  ready_ = false;
}

std::uint64_t ConflictAckermannSelector::pair_key(core::TermId lhs, core::TermId rhs) {
  if (rhs < lhs) std::swap(lhs, rhs);
  return (static_cast<std::uint64_t>(lhs) << 32U) | rhs;
}

void ConflictAckermannSelector::register_atom(core::TermId atom, sat::Lit literal) {
  const auto& term = terms_.get(atom);
  if (term.kind == core::TermKind::Equality && term.children.size() == 2 &&
      equalities_.emplace(pair_key(term.children[0], term.children[1]), literal).second) {
    // Registration is outside callbacks. Every former missing pair may be useful now.
    rejected_.clear();
    rejected_order_.clear();
  }
}

void ConflictAckermannSelector::enqueue(sat::Clause clause) {
  pending_.push_back(std::move(clause));
}

sat::Clause ConflictAckermannSelector::take_clause() {
  if (pending_.empty()) throw std::logic_error("no pending conflict Ackermann clause");
  auto clause = std::move(pending_.front());
  pending_.pop_front();
  return clause;
}

void ConflictAckermannSelector::reject(std::uint64_t key) {
  constexpr std::size_t kNegativeLimit = 4096;
  if (rejected_.size() == kNegativeLimit) {
    rejected_.erase(rejected_order_.front());
    rejected_order_.pop_front();
    statistics_.increment(util::StatKey::EufConflictAckV2NegativeEvictions);
  }
  rejected_.insert(key);
  rejected_order_.push_back(key);
  statistics_.set(
      util::StatKey::EufConflictAckV2PeakNegativePairs,
      std::max<std::uint64_t>(statistics_.get(util::StatKey::EufConflictAckV2PeakNegativePairs),
                              rejected_.size()));
}

std::optional<sat::Clause> ConflictAckermannSelector::consider(core::TermId lhs, core::TermId rhs,
                                                               bool* missing_atoms) {
  if (missing_atoms) *missing_atoms = false;
  constexpr std::size_t kPairLimit = 4096;
  constexpr std::size_t kWidthLimit = 32;
  const auto key = pair_key(lhs, rhs);
  statistics_.set(util::StatKey::EufConflictAckV2PolicyVersion, 2);
  statistics_.increment(util::StatKey::EufConflictAckV2CandidateVisits);
  if (admitted_.contains(key)) {
    statistics_.increment(util::StatKey::EufConflictAckV2AdmittedHits);
    return std::nullopt;
  }
  if (rejected_.contains(key)) {
    statistics_.increment(util::StatKey::EufConflictAckV2NegativeHits);
    return std::nullopt;
  }
  if (admitted_.size() == kPairLimit) {
    statistics_.increment(util::StatKey::EufConflictAckV2AdmissionLimit);
    return std::nullopt;
  }
  const auto& a = terms_.get(lhs);
  const auto& b = terms_.get(rhs);
  if (a.kind != core::TermKind::Application || b.kind != core::TermKind::Application ||
      a.symbol != b.symbol || a.children.size() != b.children.size() || lhs == rhs) {
    statistics_.increment(util::StatKey::EufConflictAckNonmatchingPairs);
    reject(key);
    return std::nullopt;
  }
  if (a.children.size() >= kWidthLimit) {
    statistics_.increment(util::StatKey::EufConflictAckWidthSkips);
    reject(key);
    return std::nullopt;
  }
  const auto result = equalities_.find(key);
  bool missing = false;
  sat::Clause clause;
  if (result == equalities_.end()) {
    statistics_.increment(util::StatKey::EufConflictAckMissingResult);
    if (missing_atoms) *missing_atoms = true;
    missing = true;
    if (!detailed_diagnostics_) {
      reject(key);
      return std::nullopt;
    }
  } else {
    clause.push_back(result->second);
  }
  for (std::size_t i = 0; i < a.children.size(); ++i) {
    if (a.children[i] == b.children[i]) continue;
    const auto argument = equalities_.find(pair_key(a.children[i], b.children[i]));
    if (argument == equalities_.end()) {
      if (missing_atoms) *missing_atoms = true;
      if (detailed_diagnostics_)
        statistics_.increment(util::StatKey::EufConflictAckMissingArgument);
      else
        statistics_.increment(util::StatKey::EufConflictAckV2MissingArgumentWithResult);
      missing = true;
      continue;
    }
    if (result == equalities_.end()) continue;
    const sat::Lit literal{-argument->second.value};
    if (std::none_of(clause.begin(), clause.end(),
                     [&](sat::Lit x) { return x.value == literal.value; }))
      clause.push_back(literal);
  }
  if (missing) {
    reject(key);
    return std::nullopt;
  }
  for (const auto literal : clause) {
    if (std::any_of(clause.begin(), clause.end(),
                    [&](sat::Lit x) { return x.value == -literal.value; })) {
      statistics_.increment(util::StatKey::EufConflictAckTautologies);
      reject(key);
      return std::nullopt;
    }
  }
  admitted_.insert(key);
  statistics_.increment(util::StatKey::EufConflictAckV2AdmittedPairs);
  statistics_.set(
      util::StatKey::EufConflictAckV2PeakAdmittedPairs,
      std::max<std::uint64_t>(statistics_.get(util::StatKey::EufConflictAckV2PeakAdmittedPairs),
                              admitted_.size()));
  statistics_.increment(util::StatKey::EufConflictAckReadyClauses);
  return clause;
}

}  // namespace primo::smt
