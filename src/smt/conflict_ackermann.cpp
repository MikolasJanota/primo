#include "smt/conflict_ackermann.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>

#include "core/ids.hpp"
#include "core/term.hpp"
#include "sat/clause.hpp"
#include "sat/lit.hpp"
namespace primo::smt {

std::uint64_t ConflictAckermannSelector::pair_key(core::TermId lhs, core::TermId rhs) {
  if (rhs < lhs) std::swap(lhs, rhs);
  return (static_cast<std::uint64_t>(lhs) << 32U) | rhs;
}

void ConflictAckermannSelector::register_atom(core::TermId atom, sat::Lit literal) {
  const auto& term = terms_.get(atom);
  if (term.kind == core::TermKind::Equality && term.children.size() == 2)
    equalities_.emplace(pair_key(term.children[0], term.children[1]), literal);
}

std::optional<sat::Clause> ConflictAckermannSelector::consider(core::TermId lhs, core::TermId rhs) {
  constexpr std::size_t kPairLimit = 4096;
  constexpr std::size_t kWidthLimit = 32;
  const auto key = pair_key(lhs, rhs);
  auto found = pairs_.find(key);
  if (found != pairs_.end() &&
      (found->second.ready || found->second.generation == equalities_.size())) {
    statistics_.increment(util::StatKey::EufConflictAckDuplicatePairs);
    return std::nullopt;
  }
  if (found == pairs_.end()) {
    if (pairs_.size() == kPairLimit) {
      statistics_.increment(util::StatKey::EufConflictAckCatalogLimit);
      return std::nullopt;
    }
    found = pairs_.emplace(key, Visit{}).first;
    statistics_.increment(util::StatKey::EufConflictAckSemanticPairs);
    statistics_.set(util::StatKey::EufConflictAckPeakPairs,
                    std::max<std::uint64_t>(statistics_.get(util::StatKey::EufConflictAckPeakPairs),
                                            pairs_.size()));
  }
  Visit& visit = found->second;
  visit.generation = equalities_.size();
  const auto& a = terms_.get(lhs);
  const auto& b = terms_.get(rhs);
  if (a.kind != core::TermKind::Application || b.kind != core::TermKind::Application ||
      a.symbol != b.symbol || a.children.size() != b.children.size() || lhs == rhs) {
    statistics_.increment(util::StatKey::EufConflictAckNonmatchingPairs);
    visit.ready = true;
    return std::nullopt;
  }
  if (a.children.size() >= kWidthLimit) {
    statistics_.increment(util::StatKey::EufConflictAckWidthSkips);
    visit.ready = true;
    return std::nullopt;
  }
  const auto result = equalities_.find(key);
  bool missing = false;
  sat::Clause clause;
  if (result == equalities_.end()) {
    statistics_.increment(util::StatKey::EufConflictAckMissingResult);
    missing = true;
  } else {
    clause.push_back(result->second);
  }
  for (std::size_t i = 0; i < a.children.size(); ++i) {
    if (a.children[i] == b.children[i]) continue;
    const auto argument = equalities_.find(pair_key(a.children[i], b.children[i]));
    if (argument == equalities_.end()) {
      statistics_.increment(util::StatKey::EufConflictAckMissingArgument);
      missing = true;
      continue;
    }
    const sat::Lit literal{-argument->second.value};
    if (std::none_of(clause.begin(), clause.end(),
                     [&](sat::Lit x) { return x.value == literal.value; }))
      clause.push_back(literal);
  }
  if (missing) return std::nullopt;
  visit.ready = true;
  for (const auto literal : clause) {
    if (std::any_of(clause.begin(), clause.end(),
                    [&](sat::Lit x) { return x.value == -literal.value; })) {
      statistics_.increment(util::StatKey::EufConflictAckTautologies);
      return std::nullopt;
    }
  }
  statistics_.increment(util::StatKey::EufConflictAckReadyClauses);
  return clause;
}

}  // namespace primo::smt
