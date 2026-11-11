#pragma once

#include <array>
#include <cstddef>
#include <optional>
#include <stdexcept>
#include <string_view>

#include "smt/check_sat.hpp"
#include "smt/preprocessing.hpp"
#include "theory/quantifiers/tuple_orders.hpp"

namespace primo::cli {

// Command-line name of one enumerator. A table names every enumerator of its
// enum; a value may also have aliases, and its first entry is its canonical
// name. Defaults are not recorded here: an option's default is the canonical
// name of the value its CheckSatOptions field starts with.
template <typename Enum>
struct EnumName {
  std::string_view name;
  Enum value;
};

template <typename Enum, std::size_t N>
std::string_view enum_name(const std::array<EnumName<Enum>, N>& names, Enum value) {
  for (const auto& entry : names) {
    if (entry.value == value) {
      return entry.name;
    }
  }
  throw std::logic_error("enumerator without a command-line name");
}

template <typename Enum, std::size_t N>
std::optional<Enum> parse_enum(const std::array<EnumName<Enum>, N>& names, std::string_view name) {
  for (const auto& entry : names) {
    if (entry.name == name) {
      return entry.value;
    }
  }
  return std::nullopt;
}

inline constexpr auto kIncrementalModeNames = std::to_array<EnumName<smt::IncrementalMode>>({
    {"rebuild", smt::IncrementalMode::Rebuild},
    {"assumptions", smt::IncrementalMode::Assumptions},
});

inline constexpr auto kPreprocessingModeNames = std::to_array<EnumName<smt::PreprocessingMode>>({
    {"off", smt::PreprocessingMode::Off},
    {"basic", smt::PreprocessingMode::Basic},
    {"strong", smt::PreprocessingMode::Strong},
    {"auto", smt::PreprocessingMode::Auto},
});

inline constexpr auto kAutoPreprocessSelectorNames =
    std::to_array<EnumName<smt::AutoPreprocessSelectorMode>>({
        {"legacy", smt::AutoPreprocessSelectorMode::Legacy},
        {"structural", smt::AutoPreprocessSelectorMode::Structural},
    });

inline constexpr auto kBooleanImpliesRewriteNames =
    std::to_array<EnumName<smt::BooleanImpliesRewriteMode>>({
        {"off", smt::BooleanImpliesRewriteMode::Off},
        {"clause", smt::BooleanImpliesRewriteMode::Clause},
        {"all", smt::BooleanImpliesRewriteMode::All},
    });

inline constexpr auto kQfUfPortfolioNames = std::to_array<EnumName<smt::QfUfPortfolioMode>>({
    {"off", smt::QfUfPortfolioMode::Off},
    {"basic-polarity", smt::QfUfPortfolioMode::BasicPolarity},
});

inline constexpr auto kEufPropagationNames = std::to_array<EnumName<smt::EufPropagationMode>>({
    {"auto", smt::EufPropagationMode::Auto},
    {"off", smt::EufPropagationMode::Off},
    {"conflict", smt::EufPropagationMode::Conflict},
    {"model", smt::EufPropagationMode::Model},
    {"online", smt::EufPropagationMode::Online},
});

inline constexpr auto kEufEngineNames = std::to_array<EnumName<smt::EufEngine>>({
    {"lazy", smt::EufEngine::Lazy},
    {"incremental", smt::EufEngine::Incremental},
});

inline constexpr auto kEufExplainSearchNames = std::to_array<EnumName<smt::EufExplainSearchMode>>({
    {"one-sided", smt::EufExplainSearchMode::OneSidedBfs},
    {"bidirectional", smt::EufExplainSearchMode::BidirectionalBfs},
    {"proof-forest", smt::EufExplainSearchMode::ProofForest},
    {"reoriented-proof-forest", smt::EufExplainSearchMode::ReorientedProofForest},
    {"structural-proof-forest", smt::EufExplainSearchMode::StructuralProofForest},
});

inline constexpr auto kLraPivotingRuleNames = std::to_array<EnumName<smt::LraPivotingRule>>({
    {"bland", smt::LraPivotingRule::Bland},
    {"dantzig", smt::LraPivotingRule::Dantzig},
    {"soi", smt::LraPivotingRule::Soi},
    {"sparse", smt::LraPivotingRule::Sparse},
});

inline constexpr auto kLraSoiMinimizationNames = std::to_array<EnumName<smt::LraSoiMinimization>>({
    {"quickxplain", smt::LraSoiMinimization::QuickXplain},
    {"deletion", smt::LraSoiMinimization::Deletion},
    {"none", smt::LraSoiMinimization::None},
});

inline constexpr auto kLraSoiMinimizationOrderNames =
    std::to_array<EnumName<smt::LraSoiMinimizationOrder>>({
        {"id", smt::LraSoiMinimizationOrder::Id},
        {"row-support", smt::LraSoiMinimizationOrder::RowSupport},
        {"violation", smt::LraSoiMinimizationOrder::Violation},
    });

inline constexpr auto kMixedDispatchNames = std::to_array<EnumName<smt::MixedDispatchMode>>({
    {"narrow", smt::MixedDispatchMode::Narrow},
    {"wide", smt::MixedDispatchMode::Wide},
});

inline constexpr auto kQfUfliaLiaEqualityProducerNames =
    std::to_array<EnumName<smt::QfUfliaEqualityProducer>>({
        {"legacy", smt::QfUfliaEqualityProducer::Legacy},
        {"model-unit", smt::QfUfliaEqualityProducer::ModelUnit},
    });

inline constexpr auto kQfUflraLraEqualityProducerNames =
    std::to_array<EnumName<smt::QfUflraLraEqualityProducer>>({
        {"legacy", smt::QfUflraLraEqualityProducer::Legacy},
        {"model-unit", smt::QfUflraLraEqualityProducer::ModelUnit},
        {"adaptive", smt::QfUflraLraEqualityProducer::Adaptive},
    });

inline constexpr auto kModelEqualityMutationScopeNames =
    std::to_array<EnumName<smt::ModelEqualityMutationScope>>({
        {"all", smt::ModelEqualityMutationScope::All},
        {"care", smt::ModelEqualityMutationScope::Care},
    });

// `off` is a backward-compatible alias of `exact`.
inline constexpr auto kNelsonOppenPropagationNames =
    std::to_array<EnumName<smt::NelsonOppenPropagationMode>>({
        {"exact", smt::NelsonOppenPropagationMode::Exact},
        {"off", smt::NelsonOppenPropagationMode::Exact},
        {"final", smt::NelsonOppenPropagationMode::Final},
        {"online", smt::NelsonOppenPropagationMode::Online},
    });

inline constexpr auto kModelEqualityBranchPolicyNames =
    std::to_array<EnumName<smt::ModelEqualityBranchPolicy>>({
        {"first", smt::ModelEqualityBranchPolicy::First},
        {"ranked", smt::ModelEqualityBranchPolicy::Ranked},
        {"adaptive", smt::ModelEqualityBranchPolicy::Adaptive},
        {"care-queued", smt::ModelEqualityBranchPolicy::CareQueued},
    });

inline constexpr auto kPureTheoryFilterNames = std::to_array<EnumName<smt::PureTheoryFilterMode>>({
    {"auto", smt::PureTheoryFilterMode::Auto},
    {"on", smt::PureTheoryFilterMode::On},
    {"off", smt::PureTheoryFilterMode::Off},
});

inline constexpr auto kTheoryPhaseNames = std::to_array<EnumName<smt::TheoryPhaseMode>>({
    {"negative", smt::TheoryPhaseMode::Negative},
    {"positive", smt::TheoryPhaseMode::Positive},
    {"none", smt::TheoryPhaseMode::None},
    {"polarity", smt::TheoryPhaseMode::Polarity},
    {"auto", smt::TheoryPhaseMode::Auto},
});

inline constexpr auto kTupleOrderNames = std::to_array<EnumName<theory::TupleOrder>>({
    {"max", theory::TupleOrder::MaxDigit},
    {"max-colex", theory::TupleOrder::MaxDigitColex},
    {"sum", theory::TupleOrder::SumOfDigits},
    {"leximax", theory::TupleOrder::Leximax},
    {"deepening", theory::TupleOrder::IterativeDeepening},
    {"random", theory::TupleOrder::RandomWalk},
});

}  // namespace primo::cli
