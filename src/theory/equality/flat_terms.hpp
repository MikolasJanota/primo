#pragma once

#include <cstddef>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

#include "core/ids.hpp"
#include "core/term.hpp"
#include "core/term_manager.hpp"

namespace primo::theory::equality {

using FlatTermId = std::size_t;
inline constexpr FlatTermId kInvalidFlatTermId = static_cast<FlatTermId>(-1);

struct FlatDefinition {
  FlatTermId function = kInvalidFlatTermId;
  FlatTermId argument = kInvalidFlatTermId;
  FlatTermId result = kInvalidFlatTermId;
  core::TermId original = core::kInvalidTermId;
};

struct FlatEquality {
  FlatTermId lhs = kInvalidFlatTermId;
  FlatTermId rhs = kInvalidFlatTermId;
};

class FlatTermIndex {
 public:
  explicit FlatTermIndex(const core::TermManager& terms);

  FlatTermId flatten(core::TermId term);
  std::optional<FlatEquality> flatten_equality_atom(core::TermId term);

  const std::vector<FlatDefinition>& definitions() const { return definitions_; }
  std::size_t size() const { return nodes_.size(); }
  std::optional<FlatTermId> existing_flat_term(core::TermId term) const;
  // Curry prefixes have no original SMT term. Never export their bookkeeping
  // `original` application as if it denoted the prefix itself.
  std::optional<core::TermId> original_term(FlatTermId term) const;

 private:
  enum class NodeKind {
    OriginalConstant,
    FunctionSymbol,
    Apply,
  };

  struct ApplyKey {
    FlatTermId function = kInvalidFlatTermId;
    FlatTermId argument = kInvalidFlatTermId;

    bool operator==(const ApplyKey& other) const = default;
  };

  struct ApplyKeyHash {
    std::size_t operator()(const ApplyKey& key) const;
  };

  struct Node {
    NodeKind kind = NodeKind::OriginalConstant;
    core::TermId original = core::kInvalidTermId;
    core::SymbolId symbol = core::kInvalidSymbolId;
    FlatTermId function = kInvalidFlatTermId;
    FlatTermId argument = kInvalidFlatTermId;
  };

  FlatTermId original_constant(core::TermId term);
  FlatTermId function_symbol(core::SymbolId symbol);
  FlatTermId apply(FlatTermId function, FlatTermId argument, core::TermId original);

  const core::TermManager& terms_;
  std::vector<Node> nodes_;
  std::vector<FlatDefinition> definitions_;
  std::unordered_map<core::TermId, FlatTermId> original_constants_;
  std::unordered_map<core::SymbolId, FlatTermId> function_symbols_;
  std::unordered_map<ApplyKey, FlatTermId, ApplyKeyHash> applications_;
  std::unordered_map<core::TermId, FlatTermId> flattened_terms_;
};

}  // namespace primo::theory::equality
