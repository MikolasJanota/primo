#ifdef NDEBUG
#undef NDEBUG
#endif

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <set>
#include <string_view>
#include <utility>

#include "cli/option_names.hpp"
#include "smt/check_sat.hpp"

namespace {

using primo::cli::enum_name;
using primo::cli::EnumName;
using primo::cli::parse_enum;

// Bounded coverage check for values 0..63, not exhaustive enum reflection.
// Rejecting a declared final value catches dense growth into the boundary;
// it cannot detect an unnamed negative value or a jump outside this range
// (for example, NewMode = 100). Such enum changes require a range review.
constexpr std::size_t kProbeLimit = 64;

// Whether `Probe` is a declared enumerator. C++20 has no enumerator
// reflection, but GCC and Clang end this signature with the template
// argument, spelling a declared enumerator by its qualified name and any other
// value as a cast, such as `(ns::Enum)5` or `(ns::Enum)-1`. The qualifier may
// itself contain parentheses (Clang's `(anonymous namespace)`) and a name may
// end in digits, so the value is read from the end: after its digits and sign,
// a cast leaves `)`. probe_reads_declarations() checks that this still holds.
template <typename Enum, Enum Probe>
bool is_declared_enumerator() {
  const std::string_view signature = __PRETTY_FUNCTION__;
  const std::string_view marker = "Probe = ";
  const std::size_t start = signature.rfind(marker);
  const std::size_t end = signature.rfind(']');
  assert(start != std::string_view::npos && end != std::string_view::npos && start < end);
  std::string_view argument = signature.substr(start + marker.size(), end - start - marker.size());
  while (!argument.empty() && argument.back() >= '0' && argument.back() <= '9') {
    argument.remove_suffix(1);
  }
  if (!argument.empty() && argument.back() == '-') {
    argument.remove_suffix(1);
  }
  return !argument.empty() && argument.back() != ')';
}

template <typename Enum, std::size_t... Values>
std::array<bool, sizeof...(Values)> declared_values(std::index_sequence<Values...>) {
  return {is_declared_enumerator<Enum, static_cast<Enum>(Values)>()...};
}

template <typename Enum>
std::array<bool, kProbeLimit> declared_values() {
  return declared_values<Enum>(std::make_index_sequence<kProbeLimit>{});
}

// Covers a gap, a name ending in digits, and an anonymous namespace.
enum class ProbeCheck { First, Second, Value3 = 3 };

void probe_reads_declarations() {
  const auto declared = declared_values<ProbeCheck>();
  assert(declared[0] && declared[1] && !declared[2] && declared[3]);
  for (std::size_t value = 4; value < kProbeLimit; ++value) {
    assert(!declared[value]);
  }
}

// Names are unique and parse to their entry, every declared value within the
// probed range has a canonical name that parses back to it, and unknown names
// are rejected. Named values outside the probed range also fail this check.
template <typename Enum, std::size_t N>
void check_table(const std::array<EnumName<Enum>, N>& names) {
  const auto declared = declared_values<Enum>();
  assert(!declared.back());
  std::set<std::string_view> seen;
  for (const auto& entry : names) {
    assert(seen.insert(entry.name).second);
    const auto value = static_cast<std::size_t>(entry.value);
    assert(value < kProbeLimit && declared[value]);
    assert(parse_enum(names, entry.name) == entry.value);
  }
  for (std::size_t value = 0; value < kProbeLimit; ++value) {
    if (declared[value]) {
      const auto enumerator = static_cast<Enum>(value);
      const auto named = [enumerator](const auto& entry) { return entry.value == enumerator; };
      assert(std::any_of(names.begin(), names.end(), named));
      assert(parse_enum(names, enum_name(names, enumerator)) == enumerator);
    }
  }
  assert(!parse_enum(names, "no-such-name").has_value());
}

}  // namespace

int main() {
  probe_reads_declarations();
  using namespace primo::cli;
  check_table(kExportCnfReductionNames);
  check_table(kIncrementalModeNames);
  check_table(kPreprocessingModeNames);
  check_table(kAutoPreprocessSelectorNames);
  check_table(kBooleanImpliesRewriteNames);
  check_table(kQfUfPortfolioNames);
  check_table(kEufPropagationNames);
  check_table(kEufEngineNames);
  check_table(kEufExplainSearchNames);
  check_table(kLraPivotingRuleNames);
  check_table(kLraSoiMinimizationNames);
  check_table(kLraSoiMinimizationOrderNames);
  check_table(kMixedDispatchNames);
  check_table(kQfUflraLraEqualityProducerNames);
  check_table(kModelEqualityMutationScopeNames);
  check_table(kNelsonOppenPropagationNames);
  check_table(kModelEqualityBranchPolicyNames);
  check_table(kPureTheoryFilterNames);
  check_table(kTheoryPhaseNames);
  check_table(kTupleOrderNames);
  // An alias parses to its value; the canonical name comes first.
  assert(parse_enum(kNelsonOppenPropagationNames, "off") ==
         primo::smt::NelsonOppenPropagationMode::Exact);
  assert(enum_name(kNelsonOppenPropagationNames, primo::smt::NelsonOppenPropagationMode::Exact) ==
         "exact");
  return 0;
}
