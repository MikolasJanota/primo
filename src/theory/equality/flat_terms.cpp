#include "theory/equality/flat_terms.hpp"

#include <cstddef>
#include <optional>
#include <stdexcept>

namespace primo::theory::equality {

std::size_t FlatTermIndex::ApplyKeyHash::operator()(const ApplyKey& key) const {
  std::size_t seed = key.function + 0x9e3779b97f4a7c15ULL;
  seed ^= key.argument + 0x9e3779b97f4a7c15ULL + (seed << 6U) + (seed >> 2U);
  return seed;
}

FlatTermIndex::FlatTermIndex(const core::TermManager& terms) : terms_(terms) {
  const std::size_t term_count = terms_.size();
  nodes_.reserve((term_count * 2U) + 1U);
  definitions_.reserve(term_count);
  original_constants_.reserve(term_count);
  function_symbols_.reserve(term_count);
  applications_.reserve(term_count);
  flattened_terms_.reserve(term_count);
}

FlatTermId FlatTermIndex::flatten(core::TermId term) {
  if (const auto found = flattened_terms_.find(term); found != flattened_terms_.end()) {
    return found->second;
  }

  const core::Term& node = terms_.get(term);
  FlatTermId flat = kInvalidFlatTermId;
  switch (node.kind) {
    case core::TermKind::BoolValue:
    case core::TermKind::Constant:
      flat = original_constant(term);
      break;
    case core::TermKind::Application: {
      flat = function_symbol(node.symbol);
      for (const core::TermId child : node.children) {
        flat = apply(flat, flatten(child), term);
      }
      break;
    }
    case core::TermKind::Equality:
    case core::TermKind::Not:
    case core::TermKind::And:
    case core::TermKind::Or:
    case core::TermKind::Implies:
      if (node.sort != terms_.symbols().bool_sort()) {
        throw std::invalid_argument("non-Boolean formula node is not an EUF flat term");
      }
      // Boolean formulas used as UF arguments or equality operands are
      // opaque EUF proxies. The SAT-to-true/false bridge is asserted by the
      // SMT clausifier for each such term.
      flat = original_constant(term);
      break;
    case core::TermKind::Rational:
    case core::TermKind::Plus:
    case core::TermKind::Times:
    case core::TermKind::Le:
    case core::TermKind::Lt:
      throw std::invalid_argument("QF_LRA arithmetic nodes are not EUF flat terms");
    case core::TermKind::Ite:
    case core::TermKind::IntDiv:
    case core::TermKind::IntMod:
    case core::TermKind::Abs:
      // The preprocessing leaf lowers these unconditionally, so reaching EUF
      // means lowering was skipped or leaked. Fail loudly rather than treat
      // the node as an opaque constant, which would be a silent wrong answer.
      throw std::invalid_argument("ite/div/mod/abs must be lowered before EUF flattening");
    case core::TermKind::BoundVariable:
    case core::TermKind::Forall:
    case core::TermKind::Exists:
    case core::TermKind::Pattern:
      // Quantified formulas belong to the quantifier component, never to EUF.
      throw std::invalid_argument("quantified terms are not EUF flat terms");
  }
  flattened_terms_.emplace(term, flat);
  return flat;
}

std::optional<FlatEquality> FlatTermIndex::flatten_equality_atom(core::TermId term) {
  const core::Term& node = terms_.get(term);
  if (node.kind != core::TermKind::Equality) {
    return std::nullopt;
  }
  return FlatEquality{flatten(node.children.at(0)), flatten(node.children.at(1))};
}

std::optional<FlatTermId> FlatTermIndex::existing_flat_term(core::TermId term) const {
  const auto found = flattened_terms_.find(term);
  if (found == flattened_terms_.end()) {
    return std::nullopt;
  }
  return found->second;
}

std::optional<core::TermId> FlatTermIndex::original_term(FlatTermId term) const {
  if (term >= nodes_.size()) return std::nullopt;
  const core::TermId original = nodes_[term].original;
  const auto found = flattened_terms_.find(original);
  if (found == flattened_terms_.end() || found->second != term) return std::nullopt;
  return original;
}

FlatTermId FlatTermIndex::original_constant(core::TermId term) {
  if (const auto found = original_constants_.find(term); found != original_constants_.end()) {
    return found->second;
  }
  const FlatTermId id = nodes_.size();
  nodes_.push_back(Node{NodeKind::OriginalConstant, term, core::kInvalidSymbolId,
                        kInvalidFlatTermId, kInvalidFlatTermId});
  original_constants_.emplace(term, id);
  return id;
}

FlatTermId FlatTermIndex::function_symbol(core::SymbolId symbol) {
  if (const auto found = function_symbols_.find(symbol); found != function_symbols_.end()) {
    return found->second;
  }
  const FlatTermId id = nodes_.size();
  nodes_.push_back(Node{NodeKind::FunctionSymbol, core::kInvalidTermId, symbol, kInvalidFlatTermId,
                        kInvalidFlatTermId});
  function_symbols_.emplace(symbol, id);
  return id;
}

FlatTermId FlatTermIndex::apply(FlatTermId function, FlatTermId argument, core::TermId original) {
  const ApplyKey key{function, argument};
  if (const auto found = applications_.find(key); found != applications_.end()) {
    return found->second;
  }
  const FlatTermId id = nodes_.size();
  nodes_.push_back(Node{NodeKind::Apply, original, core::kInvalidSymbolId, function, argument});
  applications_.emplace(key, id);
  definitions_.push_back(FlatDefinition{function, argument, id, original});
  return id;
}

}  // namespace primo::theory::equality
