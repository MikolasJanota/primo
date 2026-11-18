#include "smt/typed_model_builder.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

#include "core/ids.hpp"
#include "core/model.hpp"
#include "core/sort.hpp"
#include "core/symbol_table.hpp"
#include "core/term.hpp"
#include "core/term_manager.hpp"
#include "smt/model_reconstruction.hpp"
#include "theory/arithmetic/lra_atom.hpp"
#include "theory/equality/incremental_congruence_closure.hpp"
#include "util/rational.hpp"
#include "util/statistics.hpp"

namespace primo::smt {
namespace {

class ComponentModelBuilder {
 public:
  explicit ComponentModelBuilder(core::TermManager& terms) : terms_(terms), closure_(terms) {}

  std::uint64_t disequality_checks() const { return closure_.stats().disequality_checks; }

  std::shared_ptr<const core::Model> build(
      std::span<const core::TermId> assertions, std::span<const TheoryModelAssignment> assignments,
      const std::unordered_map<core::TermId, util::Rational>& numeric_values,
      const ModelConversionChain& conversions, std::size_t* completed_integers,
      std::optional<std::vector<core::SymbolId>> visible_symbols) {
    // Original adapter design: docs/model-interpretation.md. The base model
    // contains only the final problem; eliminated definitions are replayed later.
    true_ = terms_.mk_bool(true);
    false_ = terms_.mk_bool(false);
    add_root(true_);
    add_root(false_);
    closure_.assert_distinct(true_, false_);
    for (const core::TermId root : assertions) add_root(root);
    for (const auto& assignment : assignments) {
      if (assignment.filtered) continue;
      add_root(assignment.term);
      const auto& node = terms_.get(assignment.term);
      if (node.kind == core::TermKind::Equality) {
        if (assignment.value) {
          closure_.assert_equal(node.children[0], node.children[1]);
        } else {
          closure_.assert_distinct(node.children[0], node.children[1]);
        }
      } else if (node.kind == core::TermKind::Constant ||
                 node.kind == core::TermKind::Application) {
        closure_.assert_equal(node.id, assignment.value ? true_ : false_);
      }
    }
    for (const auto& [term, value] : numeric_values) {
      add_root(term);
      anchors_.emplace(term, core::ModelValue{terms_.get(term).sort, value});
    }
    conversions.replay_reverse([&](const ModelConversionStep& step) {
      if (const auto* purification = std::get_if<PurificationProxyConversion>(&step)) {
        for (const auto& [term, proxy] : purification->application_proxies) {
          add_root(term);
          add_root(proxy);
          closure_.assert_equal(term, proxy);
        }
      }
    });
    std::sort(relevant_.begin(), relevant_.end());
    std::unordered_map<std::vector<core::ModelValue>, core::TermId, core::ModelTupleHash> by_value;
    for (const core::TermId term : relevant_) {
      const auto& node = terms_.get(term);
      if (node.kind == core::TermKind::Rational) {
        anchors_.emplace(term, core::ModelValue{node.sort, node.rational_value});
      } else if (node.kind == core::TermKind::BoolValue) {
        anchors_.emplace(term, core::ModelValue{node.sort, node.bool_value});
      }
      const auto found = anchors_.find(term);
      if (found == anchors_.end()) continue;
      const auto [same, inserted] = by_value.emplace(std::vector{found->second}, term);
      if (!inserted) closure_.assert_equal(term, same->second);
      if (const auto* number = std::get_if<util::Rational>(&found->second.payload)) {
        used_numbers_.insert(*number);
      }
    }
    closure_.saturate();
    for (const core::TermId term : relevant_) {
      const auto& node = terms_.get(term);
      if (node.sort == terms_.symbols().bool_sort() &&
          (node.kind == core::TermKind::Constant || node.kind == core::TermKind::Application) &&
          !closure_.are_equal(term, true_) && !closure_.are_equal(term, false_)) {
        closure_.assert_equal(term, false_);
      }
    }
    closure_.saturate();
    if (closure_.conflict()) throw core::ModelError("inconsistent final component equalities");
    for (const core::TermId term : relevant_) {
      const auto anchor = anchors_.find(term);
      if (anchor == anchors_.end()) continue;
      const auto [found, inserted] =
          classes_.emplace(closure_.class_representative(term), anchor->second);
      if (!inserted && found->second != anchor->second) {
        throw core::ModelError("conflicting authoritative values in one component class");
      }
    }
    std::unordered_set<core::SymbolId> needed;
    if (visible_symbols) {
      needed.insert(visible_symbols->begin(), visible_symbols->end());
      for (const core::TermId term : relevant_) {
        const auto& node = terms_.get(term);
        if (node.kind == core::TermKind::Constant || node.kind == core::TermKind::Application)
          needed.insert(node.symbol);
      }
    }
    // Keep dense slots for construction, but complete only visible or relevant
    // symbols. Popped unused declarations receive no arbitrary interpretation.
    interpretations_.resize(terms_.symbols().symbols().size());
    for (const auto& symbol : terms_.symbols().symbols()) {
      if (!visible_symbols || needed.contains(symbol.id))
        interpretations_[symbol.id] = {symbol.id, default_value(symbol.result_sort), {}};
    }
    tables_.resize(interpretations_.size());
    core::ModelEvaluator base(
        terms_, [&](const core::Term& node, std::span<const core::ModelValue> arguments) {
          const core::ModelValue value = class_value(node.id);
          auto& interpretation = interpretations_.at(node.symbol);
          if (node.kind == core::TermKind::Constant) {
            interpretation.default_value = value;
          } else {
            std::vector<core::ModelValue> key(arguments.begin(), arguments.end());
            const auto [found, inserted] = tables_[node.symbol].emplace(key, value);
            if (!inserted && found->second != value) {
              throw core::ModelError("component applications disagree at an evaluated tuple for " +
                                     terms_.symbols().get_symbol(node.symbol).name);
            }
            if (inserted) interpretation.rows.push_back({std::move(key), value});
          }
          return value;
        });
    for (const core::TermId term : relevant_) {
      const auto& node = terms_.get(term);
      if (node.kind == core::TermKind::Constant || node.kind == core::TermKind::Application) {
        static_cast<void>(base.evaluate(term));
      }
    }
    // Authoritative arithmetic values must also agree with interpreted syntax.
    for (const auto& [term, value] : anchors_) {
      if (base.evaluate(term) != value) {
        throw core::ModelError("component value disagrees with interpreted term " +
                               std::to_string(term));
      }
    }
    check_assignments(assignments);
    conversions.replay_reverse([&](const ModelConversionStep& step) { replay(step); });
    if (completed_integers != nullptr) *completed_integers = completed_integers_;
    std::erase_if(interpretations_, [](const auto& interpretation) {
      return interpretation.symbol == core::kInvalidSymbolId;
    });
    return std::make_shared<const core::Model>(terms_.symbols(), std::move(interpretations_),
                                               std::move(visible_symbols));
  }

 private:
  void add_root(core::TermId root) {
    std::vector<core::TermId> pending{root};
    while (!pending.empty()) {
      const core::TermId term = pending.back();
      pending.pop_back();
      if (!seen_.insert(term).second) continue;
      relevant_.push_back(term);
      closure_.add_term(term);
      const auto& node = terms_.get(term);
      pending.insert(pending.end(), node.children.begin(), node.children.end());
    }
  }

  core::ModelValue default_value(core::SortId sort) const {
    switch (terms_.symbols().get_sort(sort).kind) {
      case core::SortKind::Bool:
        return {sort, false};
      case core::SortKind::Int:
      case core::SortKind::Real:
        return {sort, util::Rational(0)};
      case core::SortKind::Uninterpreted:
        return {sort, core::DomainValue{terms_.size()}};
    }
    throw core::ModelError("unknown sort in completion");
  }

  core::ModelValue class_value(core::TermId term) {
    const core::TermId representative = closure_.class_representative(term);
    const auto found = classes_.find(representative);
    if (found != classes_.end()) return found->second;
    const auto sort = terms_.get(term).sort;
    auto value = default_value(sort);
    if (std::holds_alternative<core::DomainValue>(value.payload)) {
      value.payload = core::DomainValue{representative};
    } else if (std::holds_alternative<util::Rational>(value.payload)) {
      while (used_numbers_.contains(next_number_)) next_number_ += util::Rational(1);
      value.payload = next_number_;
      used_numbers_.insert(next_number_);
      if (sort == terms_.symbols().int_sort()) ++completed_integers_;
    }
    classes_.emplace(representative, value);
    return value;
  }

  core::ModelValue apply(const core::Term& node,
                         std::span<const core::ModelValue> arguments) const {
    const auto& interpretation = interpretations_.at(node.symbol);
    if (node.kind == core::TermKind::Constant) return interpretation.default_value;
    const auto& table = tables_.at(node.symbol);
    const auto found =
        table.find(std::vector<core::ModelValue>(arguments.begin(), arguments.end()));
    return found == table.end() ? interpretation.default_value : found->second;
  }

  core::ModelEvaluator evaluator() const {
    return core::ModelEvaluator(
        terms_, [this](const core::Term& node, std::span<const core::ModelValue> arguments) {
          return apply(node, arguments);
        });
  }

  void check_assignments(std::span<const TheoryModelAssignment> assignments) const {
    auto evaluation = evaluator();
    for (const auto& assignment : assignments) {
      if (!assignment.filtered &&
          evaluation.evaluate(assignment.term) !=
              core::ModelValue{terms_.symbols().bool_sort(), assignment.value}) {
        throw core::ModelError("restored theory fact is false at term " +
                               std::to_string(assignment.term));
      }
    }
  }

  void replay(const ModelConversionStep& step) {
    std::visit(
        [&](const auto& conversion) {
          using Conversion = std::decay_t<decltype(conversion)>;
          if constexpr (std::is_same_v<Conversion, DirectedEqualitySubstitutionConversion> ||
                        std::is_same_v<Conversion, LiaSubstitutionConversion>) {
            bool retained = false;
            if constexpr (std::is_same_v<Conversion, DirectedEqualitySubstitutionConversion>) {
              retained = conversion.retained_facts;
            }
            auto evaluation = evaluator();
            std::vector<std::pair<core::SymbolId, core::ModelValue>> updates;
            for (const auto& [lhs, rhs] : conversion.substitutions) {
              const auto value = evaluation.evaluate(rhs);
              if (retained) {
                if (evaluation.evaluate(lhs) != value) {
                  throw core::ModelError("retained equality is false during replay");
                }
              } else {
                const auto& target = terms_.get(lhs);
                if (target.kind != core::TermKind::Constant || target.sort != value.sort) {
                  throw core::ModelError("elimination target is not a same-sort constant");
                }
                updates.emplace_back(target.symbol, value);
              }
            }
            // Evaluate the entire simultaneous step before changing any constant.
            for (const auto& [symbol, value] : updates) {
              interpretations_.at(symbol).default_value = value;
            }
          } else if constexpr (std::is_same_v<Conversion, BooleanTheoryAssignmentConversion>) {
            check_assignments(conversion.assignments);
          } else if constexpr (std::is_same_v<Conversion, MonotoneEliminationConversion>) {
            replay_monotone(conversion);
          }
        },
        step);
  }

  void replay_monotone(const MonotoneEliminationConversion& conversion) {
    // Existing one-sided reconstruction rule, not a new elimination algorithm:
    // docs/preprocessing.md, Model reconstruction. The last variable valued
    // in each discharged atom repairs it; shared atoms are not dependency cycles.
    std::vector<TheoryModelAssignment> restored_facts;
    for (const auto& entry : conversion.eliminated) {
      const auto& target = terms_.get(entry.variable);
      if (target.kind != core::TermKind::Constant || target.sort != terms_.symbols().real_sort()) {
        throw core::ModelError("monotone elimination target is not a Real constant");
      }
      auto evaluation = evaluator();
      std::optional<util::Rational> tightest;
      for (const auto& [term, truth] : entry.constraints) {
        const auto atom = theory::arithmetic::normalize_atom(terms_, term);
        const auto coefficient = atom.polynomial.terms().find(entry.variable);
        if (coefficient == atom.polynomial.terms().end() || coefficient->second.is_zero()) {
          throw core::ModelError("monotone constraint does not contain its eliminated variable");
        }
        util::Rational rest(0);
        for (const auto& [leaf, weight] : atom.polynomial.terms()) {
          if (leaf != entry.variable) {
            rest += weight * std::get<util::Rational>(evaluation.evaluate(leaf).payload);
          }
        }
        const auto bound = (atom.rhs - rest) / coefficient->second;
        if (!tightest || (entry.bounded_above ? bound < *tightest : *tightest < bound)) {
          tightest = bound;
        }
        restored_facts.push_back({term, truth, false});
      }
      const auto value =
          tightest ? *tightest + util::Rational(entry.bounded_above ? -1 : 1) : util::Rational(0);
      interpretations_.at(target.symbol).default_value = {target.sort, value};
    }
    check_assignments(restored_facts);
  }

  core::TermManager& terms_;
  theory::equality::IncrementalCongruenceClosure closure_;
  core::TermId true_ = core::kInvalidTermId;
  core::TermId false_ = core::kInvalidTermId;
  std::vector<core::TermId> relevant_;
  std::unordered_set<core::TermId> seen_;
  std::unordered_map<core::TermId, core::ModelValue> anchors_;
  std::unordered_map<core::TermId, core::ModelValue> classes_;
  std::unordered_set<util::Rational, util::RationalHash> used_numbers_;
  util::Rational next_number_{0};
  std::size_t completed_integers_ = 0;
  std::vector<core::ModelInterpretation> interpretations_;
  std::vector<
      std::unordered_map<std::vector<core::ModelValue>, core::ModelValue, core::ModelTupleHash>>
      tables_;
};

}  // namespace

std::shared_ptr<const core::Model> build_typed_model(
    core::TermManager& terms, std::span<const core::TermId> assertions,
    std::span<const TheoryModelAssignment> assignments,
    const std::unordered_map<core::TermId, util::Rational>& numeric_values,
    const ModelConversionChain& conversions, std::size_t* completed_integers,
    util::Statistics* statistics, std::optional<std::vector<core::SymbolId>> visible_symbols) {
  const auto start = std::chrono::steady_clock::now();
  ComponentModelBuilder builder(terms);
  auto model = builder.build(assertions, assignments, numeric_values, conversions,
                             completed_integers, std::move(visible_symbols));
  if (statistics != nullptr) {
    statistics->increment_dynamic(":model.congruence.disequality-checks",
                                  builder.disequality_checks());
    statistics->increment_dynamic(
        ":time.model.reconstruction-us",
        static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
                                       std::chrono::steady_clock::now() - start)
                                       .count()));
  }
  return model;
}

}  // namespace primo::smt
