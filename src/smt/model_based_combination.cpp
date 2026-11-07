#include "smt/model_based_combination.hpp"

#include <algorithm>
#include <stdexcept>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace primo::smt {
namespace {

using theory::InterfacePair;
using theory::InterfaceRelation;
using theory::InterfaceRelationValue;
using theory::SharedModelPartition;
using theory::TheoryModelProjection;

struct ProjectionIndex {
  std::unordered_map<core::TermId, std::size_t> class_by_term;
};

ModelBasedCombinationResult result_with_status(ModelBasedCombinationStatus status) {
  ModelBasedCombinationResult result;
  result.status = status;
  return result;
}

std::uint64_t pair_key(core::TermId lhs, core::TermId rhs) {
  if (rhs < lhs) {
    std::swap(lhs, rhs);
  }
  return (static_cast<std::uint64_t>(lhs) << 32U) | static_cast<std::uint64_t>(rhs);
}

ProjectionIndex index_projection(const TheoryModelProjection& projection) {
  ProjectionIndex index;
  std::size_t class_index = 0;
  for (const SharedModelPartition& partition : projection.partitions) {
    for (const theory::SharedModelClass& model_class : partition.equality_classes) {
      for (const core::TermId term : model_class.terms) {
        index.class_by_term.emplace(term, class_index);
      }
      ++class_index;
    }
  }
  return index;
}

bool projection_equal(const ProjectionIndex& projection, const InterfacePair& pair) {
  const auto lhs = projection.class_by_term.find(pair.lhs);
  const auto rhs = projection.class_by_term.find(pair.rhs);
  return lhs != projection.class_by_term.end() && rhs != projection.class_by_term.end() &&
         lhs->second == rhs->second;
}

bool token_is_current(const TheoryModelProjection& projection, std::string_view component,
                      std::uint64_t assignment_epoch) {
  return projection.token.component == component &&
         projection.token.assignment_epoch == assignment_epoch;
}

bool projection_is_complete(const TheoryModelProjection& projection,
                            std::span<const SharedInterfaceTerms> shared_terms,
                            std::span<const core::SortId> shared_sorts) {
  std::unordered_set<core::SortId> seen_sorts;
  for (const SharedModelPartition& partition : projection.partitions) {
    if (std::ranges::find(shared_sorts, partition.sort) == shared_sorts.end() ||
        !seen_sorts.insert(partition.sort).second) {
      return false;
    }
    const auto expected =
        std::ranges::find_if(shared_terms, [&partition](const SharedInterfaceTerms& candidate) {
          return candidate.sort == partition.sort;
        });
    if (expected == shared_terms.end()) {
      return false;
    }
    std::unordered_set<core::TermId> seen_terms;
    for (const theory::SharedModelClass& model_class : partition.equality_classes) {
      if (model_class.terms.empty()) {
        return false;
      }
      for (const core::TermId term : model_class.terms) {
        if (!seen_terms.insert(term).second) {
          return false;
        }
      }
    }
    if (seen_terms.size() != expected->terms.size() ||
        !std::ranges::all_of(expected->terms, [&seen_terms](core::TermId term) {
          return seen_terms.contains(term);
        })) {
      return false;
    }
  }
  return std::ranges::all_of(shared_terms, [&seen_sorts, shared_sorts](const auto& terms) {
    return std::ranges::find(shared_sorts, terms.sort) == shared_sorts.end() ||
           seen_sorts.contains(terms.sort);
  });
}

std::vector<InterfacePair> comparison_pairs(std::span<const TheoryModelProjection> projections) {
  std::vector<InterfacePair> pairs;
  std::unordered_set<std::uint64_t> seen;
  for (const TheoryModelProjection& projection : projections) {
    for (const SharedModelPartition& partition : projection.partitions) {
      for (const theory::SharedModelClass& model_class : partition.equality_classes) {
        if (model_class.terms.size() < 2) {
          continue;
        }
        const core::TermId representative = model_class.terms.front();
        for (std::size_t index = 1; index < model_class.terms.size(); ++index) {
          const core::TermId other = model_class.terms[index];
          if (seen.insert(pair_key(representative, other)).second) {
            pairs.push_back(InterfacePair{.lhs = std::min(representative, other),
                                          .rhs = std::max(representative, other),
                                          .sort = partition.sort});
          }
        }
      }
    }
  }
  std::sort(pairs.begin(), pairs.end(), [](const InterfacePair& lhs, const InterfacePair& rhs) {
    if (lhs.sort != rhs.sort) {
      return lhs.sort < rhs.sort;
    }
    return lhs.lhs == rhs.lhs ? lhs.rhs < rhs.rhs : lhs.lhs < rhs.lhs;
  });
  return pairs;
}

bool respects_relations(const TheoryModelProjection& projection,
                        std::span<const InterfaceRelation> arrangement,
                        std::span<const core::SortId> shared_sorts) {
  const ProjectionIndex index = index_projection(projection);
  return std::ranges::all_of(
      arrangement, [&index, shared_sorts](const InterfaceRelation& relation) {
        if (std::ranges::find(shared_sorts, relation.sort) == shared_sorts.end()) {
          return true;
        }
        const InterfacePair pair{relation.lhs, relation.rhs, relation.sort};
        return projection_equal(index, pair) == (relation.value == InterfaceRelationValue::Equal);
      });
}

bool is_changed_variant(const ProjectionIndex& before, const ProjectionIndex& after,
                        std::span<const InterfacePair> shared_pairs) {
  for (const InterfacePair& pair : shared_pairs) {
    const bool before_equal = projection_equal(before, pair);
    const bool after_equal = projection_equal(after, pair);
    if (before_equal != after_equal) {
      return true;
    }
  }
  return false;
}

ModelBasedCombinationResult stop_result(theory::TheoryCheckStatus status, std::string diagnostic) {
  ModelBasedCombinationStatus mapped = ModelBasedCombinationStatus::Unknown;
  switch (status) {
    case theory::TheoryCheckStatus::ResourceLimit:
      mapped = ModelBasedCombinationStatus::ResourceLimit;
      break;
    case theory::TheoryCheckStatus::Unsupported:
      mapped = ModelBasedCombinationStatus::Unsupported;
      break;
    case theory::TheoryCheckStatus::Cancelled:
      mapped = ModelBasedCombinationStatus::Cancelled;
      break;
    case theory::TheoryCheckStatus::Unknown:
      mapped = ModelBasedCombinationStatus::Unknown;
      break;
    default:
      throw std::logic_error("non-stop theory status passed to stop_result");
  }
  ModelBasedCombinationResult result = result_with_status(mapped);
  result.diagnostic = std::move(diagnostic);
  return result;
}

ModelBasedCombinationResult stop_result(theory::TheoryMutationStatus status,
                                        std::string diagnostic) {
  ModelBasedCombinationStatus mapped = ModelBasedCombinationStatus::Unknown;
  switch (status) {
    case theory::TheoryMutationStatus::ResourceLimit:
      mapped = ModelBasedCombinationStatus::ResourceLimit;
      break;
    case theory::TheoryMutationStatus::Unsupported:
      mapped = ModelBasedCombinationStatus::Unsupported;
      break;
    case theory::TheoryMutationStatus::Cancelled:
      mapped = ModelBasedCombinationStatus::Cancelled;
      break;
    case theory::TheoryMutationStatus::Unknown:
      mapped = ModelBasedCombinationStatus::Unknown;
      break;
    default:
      throw std::logic_error("non-stop mutation status passed to stop_result");
  }
  ModelBasedCombinationResult result = result_with_status(mapped);
  result.diagnostic = std::move(diagnostic);
  return result;
}

}  // namespace

ModelBasedCombinationResult ModelBasedCombinationCoordinator::reconcile(
    std::uint64_t assignment_epoch, std::span<const ModelBasedComponentRound> components,
    std::span<const theory::InterfaceRelation> arrangement,
    std::span<const SharedInterfaceTerms> shared_terms,
    const EqualityLiteralResolver& equality_literal) {
  std::vector<theory::TheoryModelProjection> projections;
  projections.reserve(components.size());

  for (const ModelBasedComponentRound& component : components) {
    if (component.solver == nullptr) {
      throw std::invalid_argument("model-based combination component is null");
    }
    ++statistics_.component_checks;
    theory::TheoryCheckResult check = component.solver->check(component.assignment, arrangement);
    switch (check.status) {
      case theory::TheoryCheckStatus::Consistent:
        break;
      case theory::TheoryCheckStatus::LearnedClause:
        ++statistics_.learned_clauses;
        {
          ModelBasedCombinationResult result =
              result_with_status(ModelBasedCombinationStatus::LearnClause);
          result.clause = std::move(check.clause);
          return result;
        }
      case theory::TheoryCheckStatus::EntailedLiteral: {
        sat::Clause clause;
        clause.reserve(check.reasons.size() + 1);
        for (const sat::Lit reason : check.reasons) {
          clause.push_back(sat::Lit{-reason.value});
        }
        clause.push_back(check.entailed_lit);
        ++statistics_.learned_clauses;
        ModelBasedCombinationResult result =
            result_with_status(ModelBasedCombinationStatus::LearnClause);
        result.clause = std::move(clause);
        return result;
      }
      case theory::TheoryCheckStatus::FreshDecision:
        ++statistics_.fresh_decisions;
        {
          ModelBasedCombinationResult result =
              result_with_status(ModelBasedCombinationStatus::FreshDecision);
          result.decision = std::move(check.decision);
          return result;
        }
      case theory::TheoryCheckStatus::ResourceLimit:
      case theory::TheoryCheckStatus::Unsupported:
      case theory::TheoryCheckStatus::Cancelled:
      case theory::TheoryCheckStatus::Unknown:
        return stop_result(check.status, std::move(check.diagnostic));
      case theory::TheoryCheckStatus::LemmasPending:
      case theory::TheoryCheckStatus::EntailedEqualities:
        // Queued lemmas are drained only by the online component driver.
        return stop_result(theory::TheoryCheckStatus::Unsupported,
                           "new consequences require the shared component driver");
    }

    if (!component.solver->properties().produces_model_projection) {
      ModelBasedCombinationResult result =
          result_with_status(ModelBasedCombinationStatus::Unsupported);
      result.diagnostic = "component does not provide a shared model projection";
      return result;
    }
    if (!token_is_current(check.model, component.solver->properties().id, assignment_epoch)) {
      ++statistics_.stale_snapshots;
      ModelBasedCombinationResult result = result_with_status(ModelBasedCombinationStatus::Unknown);
      result.diagnostic = "stale theory model snapshot";
      return result;
    }
    if (!projection_is_complete(check.model, shared_terms,
                                component.solver->properties().shared_sorts)) {
      ModelBasedCombinationResult result = result_with_status(ModelBasedCombinationStatus::Unknown);
      result.diagnostic = "incomplete or malformed theory model projection";
      return result;
    }
    if (!respects_relations(check.model, arrangement,
                            component.solver->properties().shared_sorts)) {
      ModelBasedCombinationResult result = result_with_status(ModelBasedCombinationStatus::Unknown);
      result.diagnostic = "theory model snapshot violates the SAT arrangement";
      return result;
    }
    ++statistics_.snapshots;
    projections.push_back(std::move(check.model));
  }

  if (projections.empty()) {
    return result_with_status(ModelBasedCombinationStatus::Accept);
  }

  std::vector<ProjectionIndex> projection_indexes;
  projection_indexes.reserve(projections.size());
  for (const theory::TheoryModelProjection& projection : projections) {
    projection_indexes.push_back(index_projection(projection));
  }

  std::unordered_set<std::uint64_t> care_pair_keys;
  std::vector<InterfacePair> care_pairs;
  for (std::size_t component_index = 0; component_index < components.size(); ++component_index) {
    for (const InterfacePair& pair :
         components[component_index].solver->care_pairs(projections[component_index].token)) {
      if (care_pair_keys.insert(pair_key(pair.lhs, pair.rhs)).second) {
        care_pairs.push_back(pair);
      }
    }
  }
  statistics_.care_pairs += care_pair_keys.size();
  std::vector<InterfacePair> ordered_pairs = comparison_pairs(projections);
  std::unordered_set<std::uint64_t> ordered_pair_keys;
  ordered_pair_keys.reserve(ordered_pairs.size() + care_pairs.size());
  for (const InterfacePair& pair : ordered_pairs) {
    ordered_pair_keys.insert(pair_key(pair.lhs, pair.rhs));
  }
  for (InterfacePair pair : care_pairs) {
    const bool first_value = projection_equal(projection_indexes.front(), pair);
    const bool disagreement =
        !std::ranges::all_of(projection_indexes, [&pair, first_value](const auto& model) {
          return projection_equal(model, pair) == first_value;
        });
    if (!disagreement || !ordered_pair_keys.insert(pair_key(pair.lhs, pair.rhs)).second) {
      continue;
    }
    if (pair.rhs < pair.lhs) {
      std::swap(pair.lhs, pair.rhs);
    }
    ordered_pairs.push_back(pair);
  }
  std::stable_sort(ordered_pairs.begin(), ordered_pairs.end(),
                   [&care_pair_keys](const InterfacePair& lhs, const InterfacePair& rhs) {
                     return care_pair_keys.contains(pair_key(lhs.lhs, lhs.rhs)) >
                            care_pair_keys.contains(pair_key(rhs.lhs, rhs.rhs));
                   });

  std::vector<theory::TheoryDecisionRequest> candidate_decisions;
  bool mutated_any_projection = false;
  for (std::size_t component_index = 1; component_index < components.size(); ++component_index) {
    std::vector<InterfaceRelation> required_relations(arrangement.begin(), arrangement.end());
    bool disagrees = false;
    for (const InterfacePair& pair : ordered_pairs) {
      const bool reference_equal = projection_equal(projection_indexes.front(), pair);
      const bool component_equal = projection_equal(projection_indexes[component_index], pair);
      if (component_equal != reference_equal) {
        required_relations.push_back(
            InterfaceRelation{.lhs = pair.lhs,
                              .rhs = pair.rhs,
                              .sort = pair.sort,
                              .equality_lit = {},
                              .value = reference_equal ? InterfaceRelationValue::Equal
                                                       : InterfaceRelationValue::Disequal});
        disagrees = true;
      }
    }
    if (!disagrees) {
      continue;
    }

    theory::TheoryMutationRequest request{.source = projections[component_index].token,
                                          .required_relations = std::move(required_relations)};
    ++statistics_.mutations;
    theory::TheoryMutationResult mutation = components[component_index].solver->mutate(request);
    if (mutation.status == theory::TheoryMutationStatus::Mutated) {
      // A stale revision is reported separately from a structurally invalid
      // model: it is the mistake a component makes when its projection helper
      // stamps a fixed revision, and "invalid theory model mutation" gives no
      // hint of that.
      const bool advanced_revision =
          mutation.model.token.revision > projections[component_index].token.revision;
      const bool valid_model =
          token_is_current(mutation.model, components[component_index].solver->properties().id,
                           assignment_epoch) &&
          projection_is_complete(mutation.model, shared_terms,
                                 components[component_index].solver->properties().shared_sorts) &&
          is_changed_variant(projection_indexes[component_index], index_projection(mutation.model),
                             ordered_pairs) &&
          respects_relations(mutation.model, request.required_relations,
                             components[component_index].solver->properties().shared_sorts);
      if (!advanced_revision || !valid_model) {
        ++statistics_.rejected_mutations;
        ModelBasedCombinationResult result =
            result_with_status(ModelBasedCombinationStatus::Unknown);
        result.diagnostic = advanced_revision
                                ? "invalid theory model mutation"
                                : "theory model mutation did not advance its revision";
        return result;
      }
      projections[component_index] = std::move(mutation.model);
      projection_indexes[component_index] = index_projection(projections[component_index]);
      ++statistics_.snapshots;
      mutated_any_projection = true;
      continue;
    }
    if (mutation.status == theory::TheoryMutationStatus::FixedPoint) {
      ++statistics_.fixed_points;
      candidate_decisions.insert(candidate_decisions.end(), mutation.candidate_decisions.begin(),
                                 mutation.candidate_decisions.end());
      continue;
    }
    return stop_result(mutation.status, std::move(mutation.diagnostic));
  }

  if (!candidate_decisions.empty()) {
    ModelBasedCombinationResult result =
        result_with_status(ModelBasedCombinationStatus::DecideEquality);
    std::unordered_set<std::uint64_t> resolved_pairs;
    for (const theory::TheoryDecisionRequest& candidate : candidate_decisions) {
      if (!resolved_pairs.insert(pair_key(candidate.pair.lhs, candidate.pair.rhs)).second) {
        continue;
      }
      const std::optional<sat::Lit> equality = equality_literal(candidate.pair);
      if (!equality.has_value()) {
        continue;
      }
      const sat::Lit choice = candidate.preferred_value == InterfaceRelationValue::Equal
                                  ? equality.value()
                                  : sat::Lit{-equality->value};
      result.decision_lits.push_back(choice);
    }
    if (!result.decision_lits.empty()) {
      result.decision_lit = result.decision_lits.front();
      statistics_.model_decisions += result.decision_lits.size();
      return result;
    }
  }

  if (mutated_any_projection) {
    // A mutation is validated only against the requested relations, so it may
    // merge or split shared pairs that no pre-mutation projection made
    // visible. comparison_pairs is a spanning set for the partitions it was
    // built from, and those partitions have changed, so it is recomputed here
    // before the accept decision below relies on it. New pairs are appended so
    // the care-pair priority established above is preserved.
    for (const InterfacePair& pair : comparison_pairs(projections)) {
      if (ordered_pair_keys.insert(pair_key(pair.lhs, pair.rhs)).second) {
        ordered_pairs.push_back(pair);
      }
    }
  }

  for (const InterfacePair& pair : ordered_pairs) {
    const bool first_value = projection_equal(projection_indexes.front(), pair);
    const bool agreement =
        std::ranges::all_of(projection_indexes, [&pair, first_value](const auto& model) {
          return projection_equal(model, pair) == first_value;
        });
    if (agreement) {
      continue;
    }
    ++statistics_.disagreements;
    const std::optional<sat::Lit> literal = equality_literal(pair);
    if (!literal.has_value()) {
      ModelBasedCombinationResult result =
          result_with_status(ModelBasedCombinationStatus::Unsupported);
      result.diagnostic = "cannot create SAT literal for shared model equality";
      return result;
    }
    ++statistics_.model_decisions;
    ModelBasedCombinationResult result =
        result_with_status(ModelBasedCombinationStatus::DecideEquality);
    result.decision_lit = literal.value();
    result.decision_lits.push_back(literal.value());
    return result;
  }

  return result_with_status(ModelBasedCombinationStatus::Accept);
}

}  // namespace primo::smt
