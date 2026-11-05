#include "smt/component_dispatcher.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <utility>

namespace primo::smt {
namespace {

std::size_t variable(int literal) {
  if (literal == 0 || literal == std::numeric_limits<int>::min()) {
    throw std::logic_error("invalid literal in component dispatcher");
  }
  return static_cast<std::size_t>(literal < 0 ? -literal : literal);
}

sat::Clause implication_clause(const theory::TheoryCheckResult& result) {
  sat::Clause clause{result.entailed_lit};
  for (const auto reason : result.reasons) {
    clause.push_back(sat::Lit{-reason.value});
  }
  return clause;
}

}  // namespace

ComponentDispatcher::ComponentDispatcher(std::span<theory::TheoryComponent* const> components,
                                         std::function<void(sat::Lit)> publish_phase,
                                         std::function<bool()> report_conflict,
                                         bool forgettable_clauses,
                                         std::function<void(const sat::Clause&)> observe_clause)
    : components_(components.begin(), components.end()),
      deltas_(components.size()),
      dirty_(components.size(), false),
      publish_phase_(std::move(publish_phase)),
      report_conflict_(std::move(report_conflict)),
      forgettable_clauses_(forgettable_clauses),
      observe_clause_(std::move(observe_clause)) {
  touched_.reserve(components.size());
  for (auto* component : components_) {
    if (component == nullptr || !component->descriptor().capabilities.incremental) {
      throw std::logic_error("online dispatch requires incremental components");
    }
  }
}

void ComponentDispatcher::bind_assignment_view(sat::TrailView view) {
  sat::ExternalPropagator::bind_assignment_view(view);
  for (auto* component : components_) component->bind_assignment_view(view);
}

void ComponentDispatcher::register_atoms(std::size_t component,
                                         std::span<const theory::ComponentAtom> atoms) {
  if (component >= components_.size() || assignment_view().attached()) {
    throw std::logic_error("routing may grow only at a detached preparation boundary");
  }
  for (const auto& atom : atoms) {
    const auto var = variable(atom.lit.value);
    if (var >= routes_.size()) {
      routes_.resize(var + 1);
    }
    if (routes_[var].has_value()) {
      throw std::logic_error("ambiguous component SAT variable owner");
    }
    routes_[var] = Route{component, atom};
  }
}

void ComponentDispatcher::notify_assignment(const std::vector<int>& lits) {
  touched_.clear();
  for (const auto literal : lits) {
    const auto var = variable(literal);
    if (var >= routes_.size() || !routes_[var].has_value()) {
      continue;
    }
    const auto& route = *routes_[var];
    if (deltas_[route.component].empty()) {
      touched_.push_back(route.component);
    }
    deltas_[route.component].push_back(
        {route.atom.term, route.atom.lit, (literal > 0) == (route.atom.lit.value > 0)});
    ++statistics_.routed_literals;
  }
  for (const auto component : touched_) {
    components_[component]->assign(deltas_[component]);
    deltas_[component].clear();
    dirty_[component] = true;
    ++statistics_.assignment_batches;
  }
}

void ComponentDispatcher::notify_new_decision_level() {
  for (auto* component : components_) {
    component->push_level();
  }
}

void ComponentDispatcher::notify_backtrack(std::size_t) {
  throw std::logic_error("component backtrack requires the shared removal delta");
}
void ComponentDispatcher::notify_backtrack_delta(std::size_t level,
                                                 const std::vector<int>& removed) {
  for (const int literal : removed) {
    reasons_.erase(literal);
    reasons_.erase(-literal);
  }
  for (auto* component : components_) {
    component->backtrack(level, removed);
  }
  std::fill(dirty_.begin(), dirty_.end(), true);
  external_clause_.reset();
  clause_cursor_ = 0;
  reason_literal_ = 0;
  reason_cursor_ = 0;
}

void ComponentDispatcher::accept_result(std::size_t component, theory::TheoryCheckResult result) {
  if (result.status == theory::TheoryCheckStatus::LearnedClause) {
    dirty_[component] = true;
    external_clause_ = Explanation{component, std::move(result.clause)};
    clause_cursor_ = 0;
  } else if (result.status == theory::TheoryCheckStatus::EntailedLiteral) {
    dirty_[component] = true;
    external_clause_ = Explanation{component, implication_clause(result)};
    clause_cursor_ = 0;
  } else if (result.status == theory::TheoryCheckStatus::LemmasPending) {
    // Not a failure: the owning solve loop drains the component's queue at its
    // growth boundary. No later check in this solve can be trusted before that.
    lemmas_pending_ = component;
  } else if (result.status != theory::TheoryCheckStatus::Consistent) {
    // The owning solve loop must inspect this before accepting the SAT result.
    // Resource/cancellation/unsupported is never encoded as an empty clause.
    failure_ = std::move(result);
  }
  if (external_clause_ && report_conflict_ &&
      std::all_of(external_clause_->clause.begin(), external_clause_->clause.end(),
                  [&](sat::Lit literal) {
                    return assignment_view().literal(static_cast<int>(variable(literal.value))) ==
                           -literal.value;
                  })) {
    if (report_conflict_()) {
      // Finish streaming this conflict, but request no further component work.
      // The solve runtime owns budget accounting and the final typed outcome.
      theory::TheoryCheckResult exhausted;
      exhausted.status = theory::TheoryCheckStatus::ResourceLimit;
      exhausted.diagnostic = "theory conflict limit reached";
      failure_ = std::move(exhausted);
    }
  }
}

bool ComponentDispatcher::partial_check(bool complete_assignment) {
  if (failure_.has_value() || lemmas_pending_.has_value() || external_clause_.has_value()) {
    return !external_clause_.has_value();
  }
  for (std::size_t index = 0; index < components_.size(); ++index) {
    if (!dirty_[index]) {
      continue;
    }
    // A component whose work depends on the whole assignment is consulted only
    // once SAT has one; it stays dirty until then. Consulting it mid-search let
    // a quantifier component whose atoms are root units interrupt every round
    // before the ground search could reach its conflicts.
    if (!complete_assignment && components_[index]->descriptor().capabilities.check_on_full_model) {
      continue;
    }
    dirty_[index] = false;
    ++statistics_.partial_checks;
    accept_result(index, components_[index]->check(theory::TheoryCheckEffort::Partial,
                                                   assignment_view().epoch(), {}));
    if (publish_phase_) {
      for (const auto phase : components_[index]->phase_literals()) {
        publish_phase_(phase);
      }
    }
    if (external_clause_.has_value() || failure_.has_value() || lemmas_pending_.has_value()) {
      return !external_clause_.has_value();
    }
  }
  return true;
}

bool ComponentDispatcher::cb_check_found_model(const std::vector<int>& model) {
  (void)model;
  for (std::size_t var = 1; var < routes_.size(); ++var) {
    if (routes_[var] && assignment_view().literal(static_cast<int>(var)) == 0)
      throw std::logic_error("component model contains an unnotified observed atom");
  }
  for (std::size_t index = 0; index < components_.size(); ++index) {
    if (components_[index]->descriptor().capabilities.check_on_full_model) {
      dirty_[index] = true;
    }
  }
  return partial_check(true);
}

int ComponentDispatcher::cb_decide() {
  if (!failure_.has_value() && !lemmas_pending_.has_value() && !external_clause_.has_value()) {
    for (auto* component : components_) {
      const auto literal = component->suggest_literal();
      if (literal.value != 0) {
        const auto var = variable(literal.value);
        if (var < routes_.size() && routes_[var].has_value() &&
            assignment_view().literal(static_cast<int>(var)) == 0) {
          return literal.value;
        }
      }
    }
  }
  return 0;
}

int ComponentDispatcher::cb_propagate() {
  if (failure_.has_value() || lemmas_pending_.has_value() || external_clause_.has_value()) {
    return 0;
  }
  for (std::size_t index = 0; index < components_.size(); ++index) {
    while (auto result = components_[index]->propagate()) {
      if (result->status != theory::TheoryCheckStatus::EntailedLiteral) {
        accept_result(index, std::move(*result));
        return 0;
      }
      const auto literal = result->entailed_lit.value;
      const auto var = variable(literal);
      if (var >= routes_.size() || !routes_[var].has_value()) {
        throw std::logic_error("component propagated an unregistered literal");
      }
      if (assignment_view().literal(static_cast<int>(var)) == literal) {
        continue;
      }
      if (assignment_view().literal(static_cast<int>(var)) == -literal) {
        accept_result(index, std::move(*result));
        return 0;
      }
      reasons_.insert_or_assign(literal, Explanation{index, implication_clause(*result)});
      ++statistics_.propagations;
      return literal;
    }
  }
  return 0;
}

int ComponentDispatcher::cb_add_reason_clause_lit(int propagated_lit) {
  const auto found = reasons_.find(propagated_lit);
  if (found == reasons_.end()) {
    throw std::logic_error("missing component propagation explanation");
  }
  if (reason_literal_ != propagated_lit) {
    reason_literal_ = propagated_lit;
    reason_cursor_ = 0;
  }
  if (reason_cursor_ == found->second.clause.size()) {
    if (observe_clause_) observe_clause_(found->second.clause);
    reason_literal_ = 0;
    reason_cursor_ = 0;
    return 0;
  }
  return found->second.clause[reason_cursor_++].value;
}

bool ComponentDispatcher::cb_has_external_clause(bool& is_forgettable) {
  is_forgettable = forgettable_clauses_;
  return cb_has_external_clause();
}

bool ComponentDispatcher::cb_has_external_clause() {
  static_cast<void>(partial_check());
  return external_clause_.has_value();
}

int ComponentDispatcher::cb_add_external_clause_lit() {
  if (!external_clause_.has_value()) {
    throw std::logic_error("no pending component clause");
  }
  if (clause_cursor_ == external_clause_->clause.size()) {
    if (observe_clause_) observe_clause_(external_clause_->clause);
    external_clause_.reset();
    clause_cursor_ = 0;
    return 0;
  }
  return external_clause_->clause[clause_cursor_++].value;
}

}  // namespace primo::smt
