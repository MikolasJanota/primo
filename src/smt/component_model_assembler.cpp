#include "smt/component_model_assembler.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <tuple>
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

class FragmentModelBuilder {
 public:
  explicit FragmentModelBuilder(core::TermManager& terms) : terms_(terms), closure_(terms) {}

  std::shared_ptr<const core::Model> build(
      std::span<const core::TermId> assertions, std::span<const TheoryModelAssignment> assignments,
      const std::unordered_map<core::TermId, core::ModelValue>& supplied_values,
      std::span<const core::ModelInterpretation> contributions,
      const ModelConversionChain& conversions, std::size_t* completed_integers) {
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
    for (const auto& [term, value] : supplied_values) {
      add_root(term);
      anchors_.emplace(term, value);
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
    for (const auto& contribution : contributions) {
      if (!owned_symbols_.insert(contribution.symbol).second) {
        throw core::ModelError("duplicate authoritative symbol contribution");
      }
    }
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
          !closure_.are_equal(term, true_) && !closure_.are_equal(term, false_) &&
          !owned_symbols_.contains(node.symbol)) {
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
    for (const auto& symbol : terms_.symbols().symbols()) {
      interpretations_.push_back({symbol.id, default_value(symbol.result_sort), {}});
    }
    tables_.resize(interpretations_.size());
    for (const auto& contribution : contributions) {
      interpretations_.at(contribution.symbol) = contribution;
      for (const auto& row : contribution.rows) {
        tables_.at(contribution.symbol).emplace(row.arguments, row.result);
      }
    }
    if (!contributions.empty()) {
      // Reuse core::Model's typing and duplicate-tuple checks with a complete
      // symbol environment, including defaults for unconstrained symbols.
      static_cast<void>(core::Model(terms_.symbols(), interpretations_));
    }
    core::ModelEvaluator base(
        terms_, [&](const core::Term& node, std::span<const core::ModelValue> arguments) {
          if (owned_symbols_.contains(node.symbol)) {
            const auto value = apply(node, arguments);
            const auto [known, inserted] =
                classes_.emplace(closure_.class_representative(node.id), value);
            if (!inserted && known->second != value) {
              throw core::ModelError("symbol contribution conflicts with an authoritative class");
            }
            return value;
          }
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
    return std::make_shared<const core::Model>(terms_.symbols(), std::move(interpretations_));
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
  std::unordered_set<core::SymbolId> owned_symbols_;
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

// Domain labels are private names. Only bindings of the same shared term can
// identify labels from different components; distinct labels within one
// component must never collapse. Numeric/Boolean values are already canonical.
class FragmentDomains {
 public:
  void register_value(const std::string& component, const core::ModelValue& value) {
    const auto* domain = std::get_if<core::DomainValue>(&value.payload);
    if (domain == nullptr) return;
    const Key key{component, value.sort, domain->index};
    if (!ids_.contains(key)) {
      ids_.emplace(key, parents_.size());
      parents_.push_back(parents_.size());
      origins_.push_back({{component, domain->index}});
    }
  }
  void identify(const std::string& first_component, const core::ModelValue& first,
                const std::string& second_component, const core::ModelValue& second) {
    auto lhs = root(id(first_component, first));
    auto rhs = root(id(second_component, second));
    if (lhs == rhs) return;
    for (const auto& [component, identity] : origins_[lhs]) {
      const auto found = origins_[rhs].find(component);
      if (found != origins_[rhs].end() && found->second != identity) {
        throw core::ModelError("shared bindings collapse distinct component-local domain values");
      }
    }
    parents_[rhs] = lhs;
    origins_[lhs].insert(origins_[rhs].begin(), origins_[rhs].end());
  }
  core::ModelValue canonical(const std::string& component, core::ModelValue value,
                             std::size_t offset) {
    if (std::holds_alternative<core::DomainValue>(value.payload)) {
      value.payload = core::DomainValue{offset + root(id(component, value))};
    }
    return value;
  }

 private:
  using Key = std::tuple<std::string, core::SortId, std::size_t>;
  std::size_t id(const std::string& component, const core::ModelValue& value) const {
    return ids_.at({component, value.sort, std::get<core::DomainValue>(value.payload).index});
  }
  std::size_t root(std::size_t index) {
    while (parents_[index] != index) {
      parents_[index] = parents_[parents_[index]];
      index = parents_[index];
    }
    return index;
  }
  std::map<Key, std::size_t> ids_;
  std::vector<std::size_t> parents_;
  std::vector<std::map<std::string, std::size_t>> origins_;
};

void require_binding(const core::TermManager& terms, const theory::ComponentModelBinding& binding) {
  if (binding.term >= terms.size() || terms.get(binding.term).sort != binding.value.sort) {
    throw core::ModelError("component binding has an invalid term or sort");
  }
  const auto kind = terms.symbols().get_sort(binding.value.sort).kind;
  const auto* number = std::get_if<util::Rational>(&binding.value.payload);
  if ((kind == core::SortKind::Uninterpreted) !=
          std::holds_alternative<core::DomainValue>(binding.value.payload) ||
      (kind == core::SortKind::Bool) != std::holds_alternative<bool>(binding.value.payload) ||
      ((kind == core::SortKind::Int || kind == core::SortKind::Real) && number == nullptr)) {
    throw core::ModelError("component binding has an ill-typed payload");
  }
  if (kind == core::SortKind::Int && !number->is_integer()) {
    throw core::ModelError("component exported a fractional integer value");
  }
}

}  // namespace

void validate_component_fragment(const core::TermManager& terms,
                                 const theory::ComponentDescriptor& descriptor,
                                 std::span<const theory::ComponentInterfaceScope> scopes,
                                 const theory::TheoryModelToken& token,
                                 const theory::TheoryModelProjection* projection,
                                 std::span<const theory::InterfaceRelation> arrangement,
                                 const theory::ComponentModelFragment& fragment) {
  if (fragment.token != token) {
    throw core::ModelError("component exported a stale or foreign model fragment");
  }
  for (const auto& interpretation : fragment.interpretations) {
    if (std::find(descriptor.function_symbols.begin(), descriptor.function_symbols.end(),
                  interpretation.symbol) == descriptor.function_symbols.end()) {
      throw core::ModelError("component exported an unowned symbol interpretation");
    }
  }
  std::unordered_map<core::TermId, core::ModelValue> values;
  for (const auto& binding : fragment.bindings) {
    require_binding(terms, binding);
    const auto [known, inserted] = values.emplace(binding.term, binding.value);
    if (!inserted && known->second != binding.value) {
      throw core::ModelError("component exported conflicting values for one term");
    }
  }
  for (const auto& scope : scopes) {
    const auto declaration =
        std::find_if(descriptor.shared_sorts.begin(), descriptor.shared_sorts.end(),
                     [&](const auto& item) { return item.sort == scope.sort; });
    const bool required =
        terms.symbols().get_sort(scope.sort).kind == core::SortKind::Uninterpreted ||
        (declaration != descriptor.shared_sorts.end() && declaration->authoritative_values);
    if (required) {
      for (const auto term : scope.terms) {
        if (!values.contains(term)) {
          throw core::ModelError("component omitted a required shared model binding");
        }
      }
    }
  }
  if (projection != nullptr) {
    for (const auto& partition : projection->partitions) {
      std::unordered_map<std::vector<core::ModelValue>, std::size_t, core::ModelTupleHash> classes;
      for (std::size_t index = 0; index < partition.equality_classes.size(); ++index) {
        std::optional<core::ModelValue> first;
        for (const auto term : partition.equality_classes[index].terms) {
          const auto binding = values.find(term);
          if (binding == values.end()) continue;
          if (first.has_value() && *first != binding->second) {
            throw core::ModelError("export split an accepted shared model class");
          }
          first = binding->second;
        }
        if (first.has_value() && !classes.emplace(std::vector{*first}, index).second) {
          throw core::ModelError("export merged distinct accepted shared model classes");
        }
      }
    }
  } else {
    for (const auto& relation : arrangement) {
      const auto lhs = values.find(relation.lhs), rhs = values.find(relation.rhs);
      if (lhs != values.end() && rhs != values.end() &&
          ((lhs->second == rhs->second) !=
           (relation.value == theory::InterfaceRelationValue::Equal))) {
        throw core::ModelError("export contradicts the accepted shared arrangement");
      }
    }
  }
}

std::shared_ptr<const core::Model> assemble_component_model(
    core::TermManager& terms, std::span<const core::TermId> assertions,
    std::span<const theory::ComponentModelFragment> fragments,
    const ModelConversionChain& conversions, std::size_t* completed_integers,
    util::Statistics* statistics, std::span<const theory::ComponentModelFact> propositional_facts) {
  const auto start = std::chrono::steady_clock::now();
  FragmentDomains domains;
  std::set<std::string> components;
  std::optional<std::uint64_t> epoch;
  std::vector<TheoryModelAssignment> facts;
  std::unordered_map<core::TermId, bool> fact_values;
  const auto add_fact = [&](const theory::ComponentModelFact& fact) {
    if (fact.atom >= terms.size() || terms.get(fact.atom).sort != terms.symbols().bool_sort())
      throw core::ModelError("component fact is not Boolean");
    const auto [known, inserted] = fact_values.emplace(fact.atom, fact.value);
    if (!inserted && known->second != fact.value)
      throw core::ModelError("conflicting retained component facts");
    if (inserted) facts.push_back({fact.atom, fact.value, false});
  };
  for (const auto& fact : propositional_facts) {
    if (fact.atom >= terms.size() || terms.get(fact.atom).kind != core::TermKind::Constant)
      throw core::ModelError("propositional model fact is not a Boolean constant");
    add_fact(fact);
  }

  std::unordered_map<core::TermId, std::pair<std::string, core::ModelValue>> bindings;
  std::vector<std::pair<std::string, core::ModelInterpretation>> contributions;
  for (const auto& fragment : fragments) {
    const auto& component = fragment.token.component;
    if (component.empty() || !components.insert(component).second ||
        (epoch.has_value() && *epoch != fragment.token.assignment_epoch)) {
      throw core::ModelError("duplicate component or stale fragment epoch");
    }
    epoch = fragment.token.assignment_epoch;
    for (const auto& binding : fragment.bindings) {
      require_binding(terms, binding);
      domains.register_value(component, binding.value);
      const auto [known, inserted] =
          bindings.emplace(binding.term, std::pair{component, binding.value});
      if (!inserted) {
        if (std::holds_alternative<core::DomainValue>(binding.value.payload)) {
          domains.identify(known->second.first, known->second.second, component, binding.value);
        } else if (known->second.second != binding.value) {
          throw core::ModelError("conflicting authoritative component bindings");
        }
      }
    }
    for (const auto& fact : fragment.facts) add_fact(fact);
    for (const auto& interpretation : fragment.interpretations) {
      domains.register_value(component, interpretation.default_value);
      for (const auto& row : interpretation.rows) {
        for (const auto& argument : row.arguments) domains.register_value(component, argument);
        domains.register_value(component, row.result);
      }
      contributions.emplace_back(component, interpretation);
    }
  }
  // All term-bearing inputs already exist. Reserved fragment labels sit above
  // term-representative completion IDs (including the two Boolean roots).
  const auto domain_offset = terms.size() + 2;
  std::unordered_map<core::TermId, core::ModelValue> canonical_bindings;
  for (const auto& [term, binding] : bindings) {
    canonical_bindings.emplace(term,
                               domains.canonical(binding.first, binding.second, domain_offset));
  }
  std::vector<core::ModelInterpretation> canonical_contributions;
  for (auto& [component, interpretation] : contributions) {
    interpretation.default_value =
        domains.canonical(component, interpretation.default_value, domain_offset);
    for (auto& row : interpretation.rows) {
      for (auto& argument : row.arguments)
        argument = domains.canonical(component, argument, domain_offset);
      row.result = domains.canonical(component, row.result, domain_offset);
    }
    canonical_contributions.push_back(std::move(interpretation));
  }
  auto model =
      FragmentModelBuilder(terms).build(assertions, facts, canonical_bindings,
                                        canonical_contributions, conversions, completed_integers);
  core::ModelEvaluator evaluation(terms, *model);
  for (const auto assertion : assertions) {
    if (evaluation.evaluate(assertion) != core::ModelValue{terms.symbols().bool_sort(), true}) {
      throw core::ModelError("assembled component model falsifies a final assertion");
    }
  }
  if (statistics != nullptr) {
    statistics->increment_dynamic(
        ":time.model.reconstruction-us",
        static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
                                       std::chrono::steady_clock::now() - start)
                                       .count()));
  }
  return model;
}

}  // namespace primo::smt
