#include "smt/component_model_coordinator.hpp"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

#include "theory/theory_component.hpp"

namespace primo::smt {
namespace {

using PairKey = std::tuple<core::SortId, core::TermId, core::TermId>;
using PartitionIndex = std::unordered_map<core::TermId, std::size_t>;

PairKey pair_key(const theory::InterfacePair& pair) {
  return {pair.sort, std::min(pair.lhs, pair.rhs), std::max(pair.lhs, pair.rhs)};
}

theory::InterfacePair as_pair(const theory::InterfaceRelation& relation) {
  return {relation.lhs, relation.rhs, relation.sort};
}

ModelBasedCombinationResult unknown(const char* diagnostic) {
  ModelBasedCombinationResult result;
  result.diagnostic = diagnostic;
  return result;
}

bool participates(const CombinationScope& scope, std::size_t component) {
  return std::binary_search(scope.participants.begin(), scope.participants.end(), component);
}

std::optional<PartitionIndex> index_projection(
    const theory::TheoryModelProjection& projection,
    std::span<const theory::ComponentInterfaceScope> scopes) {
  if (projection.partitions.size() != scopes.size()) {
    return std::nullopt;
  }
  std::map<core::SortId, const theory::ComponentInterfaceScope*> expected;
  for (const auto& scope : scopes) {
    expected.emplace(scope.sort, &scope);
  }
  PartitionIndex index;
  std::size_t class_id = 0;
  for (const auto& partition : projection.partitions) {
    const auto found = expected.find(partition.sort);
    if (found == expected.end()) {
      return std::nullopt;
    }
    const auto& terms = found->second->terms;
    std::size_t count = 0;
    for (const auto& equality_class : partition.equality_classes) {
      if (equality_class.terms.empty()) {
        return std::nullopt;
      }
      for (const auto term : equality_class.terms) {
        if (!std::binary_search(terms.begin(), terms.end(), term) ||
            !index.emplace(term, class_id).second) {
          return std::nullopt;
        }
        ++count;
      }
      ++class_id;
    }
    if (count != terms.size()) {
      return std::nullopt;
    }
    expected.erase(found);
  }
  return index;
}

bool agrees(const PartitionIndex& index, const theory::InterfaceRelation& relation) {
  // Only called after complete-scope validation and participant filtering.
  return (index.at(relation.lhs) == index.at(relation.rhs)) ==
         (relation.value == theory::InterfaceRelationValue::Equal);
}

void add_spanning_pairs(const theory::TheoryModelProjection& projection, std::set<PairKey>& pairs) {
  for (const auto& partition : projection.partitions) {
    for (const auto& equality_class : partition.equality_classes) {
      const auto first =
          *std::min_element(equality_class.terms.begin(), equality_class.terms.end());
      for (const auto term : equality_class.terms) {
        if (term != first) {
          pairs.emplace(partition.sort, first, term);
        }
      }
    }
  }
}

ModelBasedCombinationResult check_result(theory::TheoryCheckResult checked) {
  ModelBasedCombinationResult result;
  result.diagnostic = std::move(checked.diagnostic);
  switch (checked.status) {
    case theory::TheoryCheckStatus::Consistent:
      result.status = ModelBasedCombinationStatus::Accept;
      break;
    case theory::TheoryCheckStatus::LearnedClause:
      result.status = ModelBasedCombinationStatus::LearnClause;
      result.clause = std::move(checked.clause);
      break;
    case theory::TheoryCheckStatus::EntailedLiteral:
      result.status = ModelBasedCombinationStatus::LearnClause;
      for (const auto reason : checked.reasons) {
        result.clause.push_back(sat::Lit{-reason.value});
      }
      result.clause.push_back(checked.entailed_lit);
      break;
    case theory::TheoryCheckStatus::EntailedEqualities:
      result.status = ModelBasedCombinationStatus::PropagateEqualities;
      result.equalities = std::move(checked.equalities);
      break;
    case theory::TheoryCheckStatus::FreshDecision:
      result.status = ModelBasedCombinationStatus::FreshDecision;
      result.decision = std::move(checked.decision);
      break;
    case theory::TheoryCheckStatus::IntegerSplit:
      result.status = ModelBasedCombinationStatus::IntegerSplit;
      result.integer_split = std::move(checked.integer_split);
      break;
    case theory::TheoryCheckStatus::LemmasPending:
      // Queued lemmas are drained only at the online driver's growth boundary.
      result.status = ModelBasedCombinationStatus::Unsupported;
      break;
    case theory::TheoryCheckStatus::ResourceLimit:
      result.status = ModelBasedCombinationStatus::ResourceLimit;
      break;
    case theory::TheoryCheckStatus::Unsupported:
      result.status = ModelBasedCombinationStatus::Unsupported;
      break;
    case theory::TheoryCheckStatus::Cancelled:
      result.status = ModelBasedCombinationStatus::Cancelled;
      break;
    case theory::TheoryCheckStatus::Unknown:
      break;
  }
  return result;
}

}  // namespace

ModelBasedCombinationResult ComponentModelCoordinator::reconcile(
    const TheoryCombinationPlan& plan, std::uint64_t assignment_epoch,
    std::span<theory::TheoryComponent* const> components,
    std::span<const theory::InterfaceRelation> arrangement,
    const EqualityLiteralResolver& equality_literal) {
  tokens_.assign(components.size(), {});
  projections_.assign(components.size(), {});
  if (components.size() != plan.descriptors().size()) {
    return unknown("component collection differs from validated plan");
  }
  std::map<PairKey, theory::InterfaceRelationValue> fixed;
  std::vector<std::vector<theory::InterfaceRelation>> local_arrangements(components.size());
  std::vector<std::size_t> scope_pair_counts(plan.scopes().size(), 0);
  for (const auto& relation : arrangement) {
    const auto scope_index = plan.scope_for(as_pair(relation));
    if (!scope_index.has_value() || relation.equality_lit.value == 0) {
      return unknown("invalid interface arrangement relation");
    }
    const auto [entry, inserted] = fixed.emplace(pair_key(as_pair(relation)), relation.value);
    if (!inserted) {
      if (entry->second != relation.value) {
        return unknown("contradictory interface arrangement entries");
      }
      continue;
    }
    ++scope_pair_counts[*scope_index];
    for (const auto participant : plan.scopes()[*scope_index].participants) {
      local_arrangements[participant].push_back(relation);
    }
  }
  if (plan.policy() == CombinationPolicy::CompleteArrangement) {
    for (std::size_t index = 0; index < plan.scopes().size(); ++index) {
      const auto size = plan.scopes()[index].terms.size();
      if (scope_pair_counts[index] != size * (size - 1) / 2) {
        return unknown("complete arrangement policy requires every normalized pair");
      }
    }
  }

  auto& projections = projections_;
  std::vector<PartitionIndex> indices(components.size());
  for (std::size_t index = 0; index < components.size(); ++index) {
    if (components[index] == nullptr ||
        components[index]->descriptor() != plan.descriptors()[index]) {
      return unknown("component identity/order differs from validated plan");
    }
    ++statistics_.component_checks;
    auto checked = components[index]->check(theory::TheoryCheckEffort::FullModel, assignment_epoch,
                                            local_arrangements[index]);
    if (checked.status != theory::TheoryCheckStatus::Consistent) {
      auto result = check_result(std::move(checked));
      if (result.status == ModelBasedCombinationStatus::LearnClause) {
        ++statistics_.learned_clauses;
      }
      if (result.status == ModelBasedCombinationStatus::PropagateEqualities) {
        if (plan.descriptors()[index].capabilities.equality_propagation ==
                theory::EqualityPropagationStrength::None ||
            result.equalities.empty()) {
          return unknown("component returned an empty or unadvertised equality batch");
        }
        std::set<PairKey> seen;
        for (const auto& equality : result.equalities) {
          const auto scope = plan.scope_for(equality.pair);
          const auto key = pair_key(equality.pair);
          if (!scope.has_value() || !participates(plan.scopes()[*scope], index) ||
              equality.pair.lhs == equality.pair.rhs || !seen.insert(key).second) {
            return unknown("component returned an invalid shared equality pair");
          }
          if (const auto known = fixed.find(key);
              known != fixed.end() && known->second == theory::InterfaceRelationValue::Equal) {
            return unknown("component equality batch makes no progress on a true pair");
          }
          if (std::any_of(equality.reasons.begin(), equality.reasons.end(), [](sat::Lit reason) {
                return reason.value == 0 || reason.value == std::numeric_limits<int>::min();
              })) {
            return unknown("component equality has an invalid antecedent literal");
          }
        }
      }
      if (result.status == ModelBasedCombinationStatus::FreshDecision) {
        const auto scope = plan.scope_for(result.decision.pair);
        if (!scope.has_value() || !participates(plan.scopes()[*scope], index) ||
            fixed.contains(pair_key(result.decision.pair))) {
          return unknown("component requested an out-of-scope or already fixed decision");
        }
        ++statistics_.fresh_decisions;
      }
      return result;
    }
    if (plan.policy() == CombinationPolicy::CompleteArrangement) {
      if (checked.model.token.component != plan.descriptors()[index].id ||
          checked.model.token.assignment_epoch != assignment_epoch) {
        ++statistics_.stale_snapshots;
        return unknown("stale complete-arrangement model token");
      }
      tokens_[index] = checked.model.token;
      continue;
    }
    auto indexed = index_projection(checked.model, plan.component_scopes(index));
    if (checked.model.token.component != plan.descriptors()[index].id ||
        checked.model.token.assignment_epoch != assignment_epoch || !indexed.has_value()) {
      ++statistics_.stale_snapshots;
      return unknown("stale, incomplete or malformed component model projection");
    }
    for (const auto& relation : local_arrangements[index]) {
      if (!agrees(*indexed, relation)) {
        return unknown("component model violates its assigned interface arrangement");
      }
    }
    indices[index] = std::move(*indexed);
    projections[index] = std::move(checked.model);
    tokens_[index] = projections[index].token;
    ++statistics_.snapshots;
  }
  if (plan.policy() == CombinationPolicy::CompleteArrangement) {
    ModelBasedCombinationResult result;
    result.status = ModelBasedCombinationStatus::Accept;
    return result;
  }

  // Section 4 model mutation is optional. Each scope's earliest recipe member
  // supplies its preferred relations; foreign scopes never enter a
  // request. A fixed point is not an entailment and may still require a split.
  std::vector<theory::TheoryDecisionRequest> candidates;
  for (std::size_t index = 0; index < components.size(); ++index) {
    if (!plan.descriptors()[index].capabilities.model_mutation) {
      continue;
    }
    theory::TheoryMutationRequest request;
    request.source = projections[index].token;
    request.required_relations = local_arrangements[index];
    bool disagrees = false;
    for (const auto& scope : plan.scopes()) {
      if (!participates(scope, index) || scope.participants.front() == index) {
        continue;
      }
      std::set<PairKey> edges;
      const auto reference = scope.participants.front();
      add_spanning_pairs(projections[reference], edges);
      add_spanning_pairs(projections[index], edges);
      for (const auto& [sort, lhs, rhs] : edges) {
        const theory::InterfacePair pair{lhs, rhs, sort};
        const auto owner = plan.scope_for(pair);
        if (!owner.has_value() || &plan.scopes()[*owner] != &scope) {
          continue;
        }
        const bool equal = indices[reference].at(lhs) == indices[reference].at(rhs);
        if (equal != (indices[index].at(lhs) == indices[index].at(rhs)) &&
            !fixed.contains(pair_key(pair))) {
          request.required_relations.push_back({lhs,
                                                rhs,
                                                sort,
                                                {},
                                                equal ? theory::InterfaceRelationValue::Equal
                                                      : theory::InterfaceRelationValue::Disequal});
          disagrees = true;
        }
      }
    }
    if (!disagrees) {
      continue;
    }
    auto mutation = components[index]->mutate(request);
    if (mutation.status == theory::TheoryMutationStatus::FixedPoint) {
      ++statistics_.fixed_points;
      for (const auto& candidate : mutation.candidate_decisions) {
        const auto scope = plan.scope_for(candidate.pair);
        if (!scope.has_value() || !participates(plan.scopes()[*scope], index) ||
            fixed.contains(pair_key(candidate.pair))) {
          return unknown("mutation requested an out-of-scope or already fixed decision");
        }
        candidates.push_back(candidate);
      }
      continue;
    }
    if (mutation.status != theory::TheoryMutationStatus::Mutated) {
      ModelBasedCombinationResult result;
      result.diagnostic = std::move(mutation.diagnostic);
      switch (mutation.status) {
        case theory::TheoryMutationStatus::ResourceLimit:
          result.status = ModelBasedCombinationStatus::ResourceLimit;
          break;
        case theory::TheoryMutationStatus::Unsupported:
          result.status = ModelBasedCombinationStatus::Unsupported;
          break;
        case theory::TheoryMutationStatus::Cancelled:
          result.status = ModelBasedCombinationStatus::Cancelled;
          break;
        default:
          break;
      }
      return result;
    }
    auto indexed = index_projection(mutation.model, plan.component_scopes(index));
    if (!indexed.has_value() || mutation.model.token.component != request.source.component ||
        mutation.model.token.assignment_epoch != assignment_epoch ||
        mutation.model.token.revision <= request.source.revision ||
        !std::all_of(request.required_relations.begin(), request.required_relations.end(),
                     [&](const auto& relation) { return agrees(*indexed, relation); })) {
      ++statistics_.rejected_mutations;
      return unknown("mutation returned a stale, malformed or arrangement-violating model");
    }
    projections[index] = std::move(mutation.model);
    tokens_[index] = projections[index].token;
    indices[index] = std::move(*indexed);
    ++statistics_.mutations;
  }

  if (!candidates.empty()) {
    ModelBasedCombinationResult result;
    std::set<PairKey> seen;
    for (const auto& candidate : candidates) {
      if (!seen.insert(pair_key(candidate.pair)).second) {
        continue;
      }
      const auto literal = equality_literal(candidate.pair);
      if (!literal.has_value() || literal->value == 0) {
        return unknown("mutation decision equality could not be materialized");
      }
      result.decision_lits.push_back(candidate.preferred_value ==
                                             theory::InterfaceRelationValue::Equal
                                         ? *literal
                                         : sat::Lit{-literal->value});
    }
    result.status = ModelBasedCombinationStatus::DecideEquality;
    result.decision_lit = result.decision_lits.front();
    statistics_.model_decisions += result.decision_lits.size();
    return result;
  }

  // Preserve care-first lexicographic selection without materializing its sorted
  // union. Dropping agreeing pairs/duplicates cannot change the first conflict.
  // See "Linear ordinary-decision selection" in n-way-combination-migration.md.
  std::optional<PairKey> choice;
  const auto consider = [&](const theory::InterfacePair& pair, const CombinationScope& scope) {
    const auto& reference = indices[scope.participants.front()];
    const bool equal = reference.at(pair.lhs) == reference.at(pair.rhs);
    for (std::size_t index = 1; index < scope.participants.size(); ++index) {
      const auto& candidate = indices[scope.participants[index]];
      if ((candidate.at(pair.lhs) == candidate.at(pair.rhs)) != equal) {
        const auto key = pair_key(pair);
        if (!choice.has_value() || key < *choice) {
          choice = key;
        }
        return true;
      }
    }
    return false;
  };
  for (std::size_t index = 0; index < components.size(); ++index) {
    if (!plan.descriptors()[index].capabilities.care_pairs) {
      continue;
    }
    for (const auto& pair : components[index]->care_pairs(projections[index].token)) {
      const auto scope = plan.scope_for(pair);
      if (!scope.has_value() || !participates(plan.scopes()[*scope], index)) {
        return unknown("component care pair lies outside its normalized scope");
      }
      ++statistics_.care_pairs;
      if (!consider(pair, plan.scopes()[*scope])) {
        ++statistics_.care_pair_agreements;
      }
    }
  }
  if (!choice.has_value()) {
    // Regenerate post-mutation spanning edges only when no care disagreement
    // wins. Agreement on these edges still proves agreement of the partitions.
    for (const auto& projection : projections) {
      for (const auto& partition : projection.partitions) {
        for (const auto& equality_class : partition.equality_classes) {
          const auto first =
              *std::min_element(equality_class.terms.begin(), equality_class.terms.end());
          for (const auto term : equality_class.terms) {
            if (term == first) {
              continue;
            }
            const theory::InterfacePair pair{first, term, partition.sort};
            const auto scope = plan.scope_for(pair);
            if (!scope.has_value()) {
              return unknown("projection edge is outside normalized interface scopes");
            }
            ++statistics_.spanning_pair_checks;
            static_cast<void>(consider(pair, plan.scopes()[*scope]));
          }
        }
      }
    }
  }
  ModelBasedCombinationResult result;
  if (choice.has_value()) {
    if (fixed.contains(*choice)) {
      return unknown("model disagreement on an already assigned interface pair");
    }
    ++statistics_.disagreements;
    const auto [sort, lhs, rhs] = *choice;
    const auto literal = equality_literal({lhs, rhs, sort});
    if (!literal.has_value() || literal->value == 0) {
      return unknown("interface equality could not be materialized");
    }
    result.decision_lits.push_back(*literal);
  }
  if (result.decision_lits.empty()) {
    result.status = ModelBasedCombinationStatus::Accept;
  } else {
    result.status = ModelBasedCombinationStatus::DecideEquality;
    result.decision_lit = result.decision_lits.front();
    statistics_.model_decisions += result.decision_lits.size();
  }
  return result;
}

}  // namespace primo::smt
