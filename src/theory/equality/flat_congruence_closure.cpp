#include "theory/equality/flat_congruence_closure.hpp"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <optional>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace primo::theory::equality {

namespace {

std::size_t lit_index(sat::Lit lit) {
  const std::size_t var = static_cast<std::size_t>(lit.value < 0 ? -lit.value : lit.value);
  return (var << 1U) + (lit.value < 0 ? 1U : 0U);
}

struct ConflictTraceScope {
  explicit ConflictTraceScope(bool* enabled) : flag(enabled) {
    if (flag) *flag = true;
  }
  ~ConflictTraceScope() {
    if (flag) *flag = false;
  }
  bool* flag;
};

}  // namespace

std::size_t FlatCongruenceClosure::ApplyKeyHash::operator()(const ApplyKey& key) const {
  std::size_t seed = key.function + 0x9e3779b97f4a7c15ULL;
  seed ^= key.argument + 0x9e3779b97f4a7c15ULL + (seed << 6U) + (seed >> 2U);
  return seed;
}

std::size_t FlatCongruenceClosure::ExplanationKeyHash::operator()(const ExplanationKey& key) const {
  std::size_t seed = key.lhs + 0x9e3779b97f4a7c15ULL;
  seed ^= key.rhs + 0x9e3779b97f4a7c15ULL + (seed << 6U) + (seed >> 2U);
  return seed;
}

FlatCongruenceClosure::FlatCongruenceClosure(std::size_t node_count,
                                             FlatExplanationSearchMode explanation_search_mode,
                                             bool proof_forest_diagnostics)
    : explanation_search_mode_(explanation_search_mode),
      proof_forest_diagnostics_(proof_forest_diagnostics) {
  definitions_.reserve(node_count);
  definition_by_result_.reserve(node_count);
  lookup_.reserve(node_count);
  pending_.reserve(node_count);
  disequalities_.reserve(node_count);
  edges_.reserve(node_count);
  ensure_node_count(node_count);
}

void FlatCongruenceClosure::enable_conflict_congruence_trace(std::size_t limit) {
  if (!checkpoints_.empty() || conflict_ || conflict_trace_)
    throw std::logic_error("conflict congruence tracing must start before checkpoints/conflicts");
  if (explanation_search_mode_ == FlatExplanationSearchMode::ReorientedProofForest ||
      explanation_search_mode_ == FlatExplanationSearchMode::StructuralProofForest)
    throw std::invalid_argument(
        "conflict Ackermann prototype requires BFS or proof-forest explanations");
  // Explicit aggregate construction avoids Clang/libstdc++'s default-
  // constructibility trait issue for this private nested state type.
  if (limit > 64) throw std::invalid_argument("conflict witness limit exceeds 64");
  conflict_trace_ = ConflictTraceState{};
  conflict_trace_->limit = limit;
  conflict_trace_->seen.resize(128);
}

const ConflictCongruenceTrace* FlatCongruenceClosure::conflict_congruence_trace() const {
  return conflict_ && conflict_trace_ ? &conflict_trace_->current : nullptr;
}

void FlatCongruenceClosure::set_conflict_trace_semantic_results(
    std::vector<unsigned char> complete_results) {
  if (!conflict_trace_ || conflict_ || !checkpoints_.empty())
    throw std::logic_error("trace metadata requires an enabled consistent closure");
  conflict_trace_->semantic_results = std::move(complete_results);
}

void FlatCongruenceClosure::trace_conflict_congruence(const Edge& edge) const {
  if (!conflict_trace_ || !conflict_trace_->collecting) return;
  auto& state = *conflict_trace_;
  auto& trace = state.current;
  ++trace.visited_steps;
  FlatTermId lhs = edge.lhs_definition.result, rhs = edge.rhs_definition.result;
  if (!state.semantic_results.empty() &&
      (lhs >= state.semantic_results.size() || rhs >= state.semantic_results.size() ||
       !state.semantic_results[lhs] || !state.semantic_results[rhs])) {
    ++trace.prefix_steps;
    return;
  }
  // After the retained budget fills, count raw/filtered visits without further hashing.
  if (trace.steps.size() == state.limit) {
    ++trace.budget_full_visits;
    return;
  }
  if (rhs < lhs) std::swap(lhs, rhs);
  auto bucket = (lhs * 0x9e3779b97f4a7c15ULL ^ rhs) & 127U;
  for (std::size_t probe = 0; probe < 128; ++probe) {
    auto& slot = state.seen[bucket];
    if (slot.generation != state.generation) {
      slot = {lhs, rhs, state.generation};
      trace.steps.push_back({lhs, rhs});
      return;
    }
    if (slot.lhs == lhs && slot.rhs == rhs) {
      ++trace.duplicate_steps;
      return;
    }
    bucket = (bucket + 1) & 127U;
  }
  throw std::logic_error("bounded conflict witness table is unexpectedly full");
}

std::size_t FlatCongruenceClosure::push() {
  if (conflict_trace_) conflict_trace_->checkpoints.push_back(conflict_trace_->current);
  checkpoints_.push_back(
      Checkpoint{parent_.size(), definitions_.size(), pending_.size(), disequalities_.size(),
                 edges_.size(), changed_terms_.size(), changed_terms_generation_, trail_.size(),
                 flat_vector_trail_.size(), size_vector_trail_.size(), conflict_, stats_});
  stats_.max_checkpoint_depth = std::max(stats_.max_checkpoint_depth, checkpoints_.size());
  // Kept in lockstep with checkpoints_ so the same index is valid for both;
  // cheap no-op when reoriented_proof_forest_ is empty (mode not selected).
  reoriented_proof_forest_.push();
  structural_proof_forest_.push();
  return checkpoints_.size() - 1;
}

void FlatCongruenceClosure::pop(std::size_t checkpoint) {
  if (protected_checkpoint_.has_value() && checkpoint <= protected_checkpoint_.value()) {
    throw std::logic_error("attempted to pop a protected flat congruence closure checkpoint");
  }
  if (checkpoint >= checkpoints_.size()) {
    throw std::out_of_range("flat congruence closure checkpoint out of range");
  }
  Checkpoint state = std::move(checkpoints_[checkpoint]);
  checkpoints_.resize(checkpoint);
  while (trail_.size() > state.trail_size) {
    restore_trail_entry(trail_.back());
    trail_.pop_back();
  }
  flat_vector_trail_.resize(state.flat_vector_trail_size);
  size_vector_trail_.resize(state.size_vector_trail_size);
  definitions_.resize(state.definitions_size);
  pending_.resize(state.pending_size);
  disequalities_.resize(state.disequalities_size);
  edges_.resize(state.edges_size);
  if (state.changed_terms_generation == changed_terms_generation_) {
    changed_terms_.resize(state.changed_terms_size);
  } else {
    changed_terms_.clear();
  }
  parent_.resize(state.node_count);
  class_size_.resize(state.node_count);
  class_members_.resize(state.node_count);
  use_lists_.resize(state.node_count);
  use_list_marks_.resize(state.node_count);
  disequality_lists_.resize(state.node_count);
  edge_adjacency_.resize(state.node_count);
  // Match ensure_node_count(): disabled explanation state must stay unallocated
  // during rollback, including rollback from a temporary model scope.
  if (proof_forest_diagnostics_ ||
      explanation_search_mode_ == FlatExplanationSearchMode::ProofForest) {
    proof_parent_.resize(state.node_count);
    proof_parent_label_endpoint_.resize(state.node_count);
    proof_parent_edge_.resize(state.node_count);
  }
  reoriented_proof_forest_.pop(checkpoint);
  structural_proof_forest_.pop(checkpoint);
  conflict_ = std::move(state.conflict);
  if (conflict_trace_) {
    const auto& saved = conflict_trace_->checkpoints.at(checkpoint);
    conflict_trace_->current.visited_steps = saved.visited_steps;
    conflict_trace_->current.prefix_steps = saved.prefix_steps;
    conflict_trace_->current.duplicate_steps = saved.duplicate_steps;
    conflict_trace_->current.budget_full_visits = saved.budget_full_visits;
    conflict_trace_->current.steps.assign(saved.steps.begin(), saved.steps.end());
    conflict_trace_->checkpoints.resize(checkpoint);
  }
  stats_ = state.stats;
  ++stats_.backtracks;
}

void FlatCongruenceClosure::protect_checkpoint(std::size_t checkpoint) {
  if (protected_checkpoint_.has_value() || checkpoint >= checkpoints_.size()) {
    throw std::logic_error("invalid flat congruence closure checkpoint protection");
  }
  protected_checkpoint_ = checkpoint;
}

void FlatCongruenceClosure::release_protected_checkpoint(std::size_t checkpoint) {
  if (!protected_checkpoint_.has_value() || protected_checkpoint_.value() != checkpoint) {
    throw std::logic_error("mismatched flat congruence closure checkpoint protection");
  }
  protected_checkpoint_.reset();
}

void FlatCongruenceClosure::ensure_node_count(std::size_t node_count) {
  const std::size_t old_size = parent_.size();
  if (old_size >= node_count) {
    return;
  }
  stats_.terms += node_count - old_size;
  parent_.resize(node_count);
  class_size_.resize(node_count, 1);
  class_members_.resize(node_count);
  use_lists_.resize(node_count);
  use_list_marks_.resize(node_count);
  disequality_lists_.resize(node_count);
  edge_adjacency_.resize(node_count);
  if (proof_forest_diagnostics_ ||
      explanation_search_mode_ == FlatExplanationSearchMode::ProofForest) {
    proof_parent_.resize(node_count, kInvalidFlatTermId);
    proof_parent_label_endpoint_.resize(node_count, kInvalidFlatTermId);
    proof_parent_edge_.resize(node_count, edges_.size());
  }
  if (explanation_search_mode_ == FlatExplanationSearchMode::ReorientedProofForest) {
    reoriented_proof_forest_.ensure_node_count(node_count);
  }
  if (explanation_search_mode_ == FlatExplanationSearchMode::StructuralProofForest) {
    structural_proof_forest_.ensure_node_count(node_count);
  }
  for (std::size_t i = old_size; i < node_count; ++i) {
    parent_[i] = i;
    class_size_[i] = 1;
    class_members_[i] = {i};
  }
}

void FlatCongruenceClosure::add_definition(const FlatDefinition& definition) {
  ensure_node_count(std::max({definition.function, definition.argument, definition.result}) + 1);
  if (const auto found = definition_by_result_.find(definition.result);
      found != definition_by_result_.end()) {
    const Definition& existing = definitions_[found->second];
    if (existing.function != definition.function || existing.argument != definition.argument) {
      throw std::logic_error("flat congruence closure result has conflicting definitions");
    }
    return;
  }
  ++stats_.flat_definitions;
  const auto inserted_definition =
      definition_by_result_.emplace(definition.result, definitions_.size());
  if (inserted_definition.second && !checkpoints_.empty()) {
    TrailEntry entry;
    entry.kind = TrailKind::DefinitionByResultInsertion;
    entry.index = definition.result;
    trail_.push_back(std::move(entry));
    ++stats_.trail_entries;
  }
  definitions_.push_back(Definition{definition.function, definition.argument, definition.result});

  const Definition& stored = definitions_.back();
  const ApplyKey key = signature(stored);
  ++stats_.lookup_probes;
  if (const auto found = lookup_.find(key); found != lookup_.end()) {
    ++stats_.lookup_hits;
    enqueue_congruence(stored, definitions_[definition_by_result_.at(found->second)]);
    propagate();
    return;
  }
  const auto inserted_lookup = lookup_.emplace(key, stored.result);
  if (inserted_lookup.second && !checkpoints_.empty()) {
    TrailEntry entry;
    entry.kind = TrailKind::LookupInsertion;
    entry.lookup_key = key;
    trail_.push_back(std::move(entry));
    ++stats_.trail_entries;
  }
  const FlatTermId function_repr = representative(stored.function);
  const FlatTermId argument_repr = representative(stored.argument);
  append_use_tracked(function_repr, stored.result);
  if (argument_repr != function_repr) {
    append_use_tracked(argument_repr, stored.result);
  } else {
    ++stats_.use_list_duplicate_skips;
  }
}

void FlatCongruenceClosure::assert_equal(FlatTermId lhs, FlatTermId rhs,
                                         std::vector<sat::Lit> reason) {
  ensure_node_count(std::max(lhs, rhs) + 1);
  enqueue(lhs, rhs, std::move(reason), false);
  propagate();
}

void FlatCongruenceClosure::assert_distinct(FlatTermId lhs, FlatTermId rhs,
                                            std::vector<sat::Lit> reason) {
  ensure_node_count(std::max(lhs, rhs) + 1);
  const std::size_t disequality_index = disequalities_.size();
  disequalities_.push_back(Disequality{lhs, rhs, std::move(reason)});
  const FlatTermId lhs_repr = representative(lhs);
  const FlatTermId rhs_repr = representative(rhs);
  if (!checkpoints_.empty()) {
    TrailEntry entry;
    entry.kind = TrailKind::DisequalityListPop;
    entry.index = lhs_repr;
    trail_.push_back(std::move(entry));
    ++stats_.trail_entries;
  }
  disequality_lists_[lhs_repr].push_back(disequality_index);
  if (lhs_repr != rhs_repr) {
    if (!checkpoints_.empty()) {
      TrailEntry entry;
      entry.kind = TrailKind::DisequalityListPop;
      entry.index = rhs_repr;
      trail_.push_back(std::move(entry));
      ++stats_.trail_entries;
    }
    disequality_lists_[rhs_repr].push_back(disequality_index);
  }
  check_disequality(disequalities_.back());
}

bool FlatCongruenceClosure::are_equal(FlatTermId lhs, FlatTermId rhs) {
  ensure_node_count(std::max(lhs, rhs) + 1);
  propagate();
  return representative(lhs) == representative(rhs);
}

std::size_t FlatCongruenceClosure::class_count(const std::vector<FlatTermId>& terms) const {
  std::unordered_set<FlatTermId> representatives;
  representatives.reserve(terms.size());
  for (const FlatTermId term : terms) {
    if (term >= parent_.size()) {
      throw std::out_of_range("flat congruence closure term out of range");
    }
    representatives.insert(representative(term));
  }
  return representatives.size();
}

std::vector<std::vector<FlatTermId>> FlatCongruenceClosure::equivalence_classes(
    const std::vector<FlatTermId>& terms) const {
  std::unordered_map<FlatTermId, std::vector<FlatTermId>> terms_by_representative;
  terms_by_representative.reserve(terms.size());
  for (const FlatTermId term : terms) {
    if (term >= parent_.size()) {
      throw std::out_of_range("flat congruence closure term out of range");
    }
    terms_by_representative[representative(term)].push_back(term);
  }

  std::vector<std::vector<FlatTermId>> classes;
  classes.reserve(terms_by_representative.size());
  for (auto& [representative, equal_terms] : terms_by_representative) {
    static_cast<void>(representative);
    std::sort(equal_terms.begin(), equal_terms.end());
    equal_terms.erase(std::unique(equal_terms.begin(), equal_terms.end()), equal_terms.end());
    classes.push_back(std::move(equal_terms));
  }
  std::sort(classes.begin(), classes.end());
  return classes;
}

void FlatCongruenceClosure::drain_changed_terms(std::vector<FlatTermId>& out) {
  ++stats_.changed_term_drains;
  stats_.changed_terms_delivered += changed_terms_.size();
  out.insert(out.end(), changed_terms_.begin(), changed_terms_.end());
  changed_terms_.clear();
  if (changed_terms_generation_ == std::numeric_limits<std::uint64_t>::max()) {
    // A drain consumes every saved notification prefix, so no open checkpoint
    // has a live prefix left. Record that directly, as an empty prefix at the
    // rebased live generation, rather than by stamping a sentinel generation:
    // any fixed sentinel is a value the counter reaches again, and a pop that
    // then took the resize branch would grow changed_terms_ with default
    // entries and deliver them as changed terms. This depth-linear path is
    // reachable only after 2^64 drains.
    changed_terms_generation_ = 0;
    for (Checkpoint& checkpoint : checkpoints_) {
      checkpoint.changed_terms_size = 0;
      checkpoint.changed_terms_generation = changed_terms_generation_;
    }
    ++stats_.changed_term_generation_rebases;
  } else {
    ++changed_terms_generation_;
  }
}

FlatTermId FlatCongruenceClosure::representative(FlatTermId term) const {
  // Every call site validates term against parent_.size() before reaching
  // here, either via ensure_node_count (assert_equal, assert_distinct,
  // are_equal, add_definition) or an explicit bounds check earlier in the
  // same expression (explain_equal_into, explain_equal_with_proof_forest_into).
  // gprof on NEQ016_size7 showed 154.7M calls to this function; the
  // always-live throw path was preventing the optimizer from collapsing it to
  // a single array load. assert() keeps the invariant checked under
  // asan-test/debug builds while costing nothing in Release (NDEBUG).
  assert(term < parent_.size() && "flat congruence closure term out of range");
  return parent_[term];
}

FlatCongruenceClosure::ApplyKey FlatCongruenceClosure::signature(
    const Definition& definition) const {
  return ApplyKey{representative(definition.function), representative(definition.argument)};
}

void FlatCongruenceClosure::enqueue(FlatTermId lhs, FlatTermId rhs, std::vector<sat::Lit> reason,
                                    bool congruence) {
  PendingMerge pending;
  pending.lhs = lhs;
  pending.rhs = rhs;
  pending.reason = std::move(reason);
  pending.congruence = congruence;
  pending_.push_back(std::move(pending));
}

void FlatCongruenceClosure::enqueue_congruence(const Definition& lhs, const Definition& rhs) {
  PendingMerge pending;
  pending.lhs = lhs.result;
  pending.rhs = rhs.result;
  pending.congruence = true;
  pending.lhs_definition = lhs;
  pending.rhs_definition = rhs;
  pending_.push_back(std::move(pending));
}

void FlatCongruenceClosure::propagate() {
  while (!pending_.empty() && !conflict_.has_value()) {
    PendingMerge pending = std::move(pending_.back());
    pending_.pop_back();
    ++stats_.pending_pops;
    if (representative(pending.lhs) == representative(pending.rhs)) {
      continue;
    }
    merge_classes(std::move(pending));
  }
}

void FlatCongruenceClosure::merge_classes(FlatTermId lhs, FlatTermId rhs,
                                          std::vector<sat::Lit> reason, bool congruence) {
  PendingMerge pending;
  pending.lhs = lhs;
  pending.rhs = rhs;
  pending.reason = std::move(reason);
  pending.congruence = congruence;
  merge_classes(std::move(pending));
}

void FlatCongruenceClosure::merge_classes(PendingMerge pending) {
  FlatTermId lhs = pending.lhs;
  FlatTermId rhs = pending.rhs;
  FlatTermId lhs_repr = representative(lhs);
  FlatTermId rhs_repr = representative(rhs);
  if (lhs_repr == rhs_repr) {
    return;
  }
  if (class_size_[lhs_repr] > class_size_[rhs_repr]) {
    std::swap(lhs_repr, rhs_repr);
  }
  const FlatTermId proof_from_endpoint =
      representative(pending.lhs) == lhs_repr ? pending.lhs : pending.rhs;
  const FlatTermId proof_to = representative(pending.lhs) == lhs_repr ? pending.rhs : pending.lhs;

  const std::size_t edge_index = edges_.size();
  edges_.push_back(Edge{lhs, rhs, std::move(pending.reason), pending.congruence,
                        pending.lhs_definition, pending.rhs_definition});
  if (!checkpoints_.empty()) {
    TrailEntry lhs_entry;
    lhs_entry.kind = TrailKind::EdgeAdjacencyPop;
    lhs_entry.index = lhs;
    trail_.push_back(std::move(lhs_entry));
    ++stats_.trail_entries;
  }
  edge_adjacency_[lhs].push_back(edge_index);
  if (!checkpoints_.empty()) {
    TrailEntry rhs_entry;
    rhs_entry.kind = TrailKind::EdgeAdjacencyPop;
    rhs_entry.index = rhs;
    trail_.push_back(std::move(rhs_entry));
    ++stats_.trail_entries;
  }
  edge_adjacency_[rhs].push_back(edge_index);
  const std::size_t merged_member_count = class_members_[lhs_repr].size();
  for (const FlatTermId member : class_members_[lhs_repr]) {
    changed_terms_.push_back(member);
    parent_[member] = rhs_repr;
    class_members_[rhs_repr].push_back(member);
  }
  if (!checkpoints_.empty()) {
    TrailEntry entry;
    entry.kind = TrailKind::ClassMembersMerge;
    entry.index = rhs_repr;
    entry.flat_value = lhs_repr;
    entry.size_value = merged_member_count;
    trail_.push_back(std::move(entry));
    ++stats_.trail_entries;
    stats_.class_member_trail_entries_saved += merged_member_count;
  }
  class_members_[lhs_repr].clear();
  push_class_size_change(rhs_repr);
  push_class_size_change(lhs_repr);
  class_size_[rhs_repr] += class_size_[lhs_repr];
  class_size_[lhs_repr] = 0;
  ++stats_.merges;
  if (pending.congruence) {
    ++stats_.congruence_merges;
  }
  if (proof_forest_diagnostics_ ||
      explanation_search_mode_ == FlatExplanationSearchMode::ProofForest) {
    record_proof_parent(lhs_repr, proof_to, proof_from_endpoint, edge_index);
  }
  if (explanation_search_mode_ == FlatExplanationSearchMode::ReorientedProofForest) {
    // Section 4's Union(e, e'): e is the raw endpoint on the smaller-class
    // side (proof_from_endpoint), matching the paper's "insert edge a -> b"
    // where a, b are the equation's own endpoints, not representatives.
    ProofForestLabel label;
    if (pending.congruence) {
      label.congruence = std::make_pair(
          ProofForestObligation{pending.lhs_definition.function, pending.rhs_definition.function},
          ProofForestObligation{pending.lhs_definition.argument, pending.rhs_definition.argument});
    } else {
      label.reason = edge_index;
    }
    reoriented_proof_forest_.union_nodes(proof_from_endpoint, proof_to, std::move(label));
  }
  if (explanation_search_mode_ == FlatExplanationSearchMode::StructuralProofForest) {
    if (pending.congruence) {
      // Section 5.5: pending.lhs is always the newly discovered structural-
      // class member and pending.rhs its class's existing canonical member
      // (enqueue_congruence's callers always pass the arriving definition
      // as lhs and the lookup-table hit as rhs) -- coalesce rather than
      // adding a tree edge. Unlike proof_from_endpoint/proof_to above, this
      // is not swapped by the class-size wlog choice: structural-class
      // canonicalness is unrelated to live class-list size.
      ProofForestLabel label;
      label.congruence = std::make_pair(
          ProofForestObligation{pending.lhs_definition.function, pending.rhs_definition.function},
          ProofForestObligation{pending.lhs_definition.argument, pending.rhs_definition.argument});
      structural_proof_forest_.coalesce_into_structural_class(pending.lhs, pending.rhs,
                                                              std::move(label));
    } else {
      // A direct equation's own endpoints may themselves be non-canonical
      // structural-class members (paper's Example 23: "a1=c" where c was
      // already coalesced) -- using resolve() here (as an earlier version
      // of this code did) would attach `edge_index`'s reason to an edge
      // between the *resolved* nodes while silently dropping the endpoint's
      // own bridge fact, unsound exactly like coalescing a twice-aliased
      // member (see ProofForest::materialize's comment). Materialize both
      // raw endpoints into real tree nodes first, then Union them directly
      // -- Section 4's own path-reversal handles whatever tree position
      // each one is already in.
      structural_proof_forest_.materialize(proof_from_endpoint);
      structural_proof_forest_.materialize(proof_to);
      ProofForestLabel label;
      label.reason = edge_index;
      structural_proof_forest_.union_nodes(proof_from_endpoint, proof_to, std::move(label));
    }
  }
  check_disequalities_touching(lhs_repr, rhs_repr);
  if (conflict_.has_value()) {
    return;
  }
  process_use_list(lhs_repr, rhs_repr);
  // Union with the empty losing list changes no state and needs no undo payload.
  if (disequality_lists_[lhs_repr].empty()) return;
  std::vector<std::size_t> merged_disequalities;
  merged_disequalities.reserve(disequality_lists_[rhs_repr].size() +
                               disequality_lists_[lhs_repr].size());
  std::merge(disequality_lists_[rhs_repr].begin(), disequality_lists_[rhs_repr].end(),
             disequality_lists_[lhs_repr].begin(), disequality_lists_[lhs_repr].end(),
             std::back_inserter(merged_disequalities));
  merged_disequalities.erase(std::unique(merged_disequalities.begin(), merged_disequalities.end()),
                             merged_disequalities.end());
  if (!checkpoints_.empty()) {
    TrailEntry rhs_entry;
    rhs_entry.kind = TrailKind::DisequalityListRestore;
    rhs_entry.index = rhs_repr;
    rhs_entry.payload_index = size_vector_trail_.size();
    size_vector_trail_.push_back(disequality_lists_[rhs_repr]);
    trail_.push_back(std::move(rhs_entry));
    ++stats_.trail_entries;
    TrailEntry lhs_entry;
    lhs_entry.kind = TrailKind::DisequalityListRestore;
    lhs_entry.index = lhs_repr;
    lhs_entry.payload_index = size_vector_trail_.size();
    size_vector_trail_.push_back(disequality_lists_[lhs_repr]);
    trail_.push_back(std::move(lhs_entry));
    ++stats_.trail_entries;
  }
  disequality_lists_[rhs_repr] = std::move(merged_disequalities);
  disequality_lists_[lhs_repr].clear();
}

void FlatCongruenceClosure::process_use_list(FlatTermId old_repr, FlatTermId new_repr) {
  std::vector<FlatTermId> affected;
  if (checkpoints_.empty()) affected.swap(use_lists_[old_repr]);
  if (!checkpoints_.empty()) {
    TrailEntry entry;
    entry.kind = TrailKind::UseListRestore;
    entry.index = old_repr;
    entry.payload_index = flat_vector_trail_.size();
    flat_vector_trail_.push_back(std::move(use_lists_[old_repr]));
    use_lists_[old_repr].clear();
    trail_.push_back(std::move(entry));
    ++stats_.trail_entries;
  }
  // enqueue_congruence only queues: the payload arena cannot grow during this loop.
  // Save its index rather than retaining a reference across the insertion above.
  const auto& affected_uses =
      checkpoints_.empty() ? affected : flat_vector_trail_[trail_.back().payload_index];
  ++use_list_generation_;
  if (use_list_generation_ == 0) {
    std::fill(use_list_marks_.begin(), use_list_marks_.end(), 0);
    ++use_list_generation_;
  }
  for (const FlatTermId result : affected_uses) {
    if (use_list_marks_[result] == use_list_generation_) {
      ++stats_.affected_use_list_dedup;
      ++stats_.use_list_duplicate_skips;
      continue;
    }
    use_list_marks_[result] = use_list_generation_;
    const auto definition_it = definition_by_result_.find(result);
    if (definition_it == definition_by_result_.end()) {
      continue;
    }
    const Definition& definition = definitions_[definition_it->second];
    const ApplyKey key = signature(definition);
    ++stats_.lookup_probes;
    if (const auto found = lookup_.find(key); found != lookup_.end()) {
      ++stats_.lookup_hits;
      if (representative(found->second) != representative(result)) {
        const Definition& other = definitions_[definition_by_result_.at(found->second)];
        enqueue_congruence(definition, other);
      }
      append_use_tracked(new_repr, result);
      continue;
    }
    const auto inserted_lookup = lookup_.emplace(key, result);
    if (inserted_lookup.second && !checkpoints_.empty()) {
      TrailEntry entry;
      entry.kind = TrailKind::LookupInsertion;
      entry.lookup_key = key;
      trail_.push_back(std::move(entry));
      ++stats_.trail_entries;
    }
    append_use_tracked(new_repr, result);
  }
}

void FlatCongruenceClosure::append_use_tracked(FlatTermId representative, FlatTermId result) {
  std::vector<FlatTermId>& use_list = use_lists_[representative];
  if (!checkpoints_.empty()) {
    TrailEntry entry;
    entry.kind = TrailKind::UseListPop;
    entry.index = representative;
    trail_.push_back(std::move(entry));
    ++stats_.trail_entries;
  }
  use_list.push_back(result);
  ++stats_.use_list_appends;
  stats_.max_use_list_size = std::max(stats_.max_use_list_size, use_list.size());
}

void FlatCongruenceClosure::push_class_size_change(FlatTermId term) {
  if (checkpoints_.empty()) {
    return;
  }
  TrailEntry entry;
  entry.kind = TrailKind::ClassSize;
  entry.index = term;
  entry.size_value = class_size_[term];
  trail_.push_back(std::move(entry));
  ++stats_.trail_entries;
}

void FlatCongruenceClosure::restore_trail_entry(TrailEntry& entry) {
  switch (entry.kind) {
    case TrailKind::ClassSize:
      if (entry.index < class_size_.size()) {
        class_size_[entry.index] = entry.size_value;
      }
      return;
    case TrailKind::ClassMembersMerge: {
      std::vector<FlatTermId>& kept = class_members_[entry.index];
      std::vector<FlatTermId>& restored = class_members_[entry.flat_value];
      const std::size_t restore_count = entry.size_value;
      const std::size_t first_restored = kept.size() - restore_count;
      restored.assign(kept.begin() + static_cast<std::ptrdiff_t>(first_restored), kept.end());
      for (const FlatTermId member : restored) {
        parent_[member] = entry.flat_value;
      }
      kept.resize(first_restored);
      return;
    }
    case TrailKind::UseListRestore:
      use_lists_[entry.index] = std::move(flat_vector_trail_[entry.payload_index]);
      return;
    case TrailKind::UseListPop:
      use_lists_[entry.index].pop_back();
      return;
    case TrailKind::DisequalityListRestore:
      disequality_lists_[entry.index] = std::move(size_vector_trail_[entry.payload_index]);
      return;
    case TrailKind::DisequalityListPop:
      disequality_lists_[entry.index].pop_back();
      return;
    case TrailKind::EdgeAdjacencyPop:
      edge_adjacency_[entry.index].pop_back();
      return;
    case TrailKind::LookupInsertion:
      lookup_.erase(entry.lookup_key);
      return;
    case TrailKind::DefinitionByResultInsertion:
      definition_by_result_.erase(static_cast<FlatTermId>(entry.index));
      return;
    case TrailKind::ProofParent:
      if (entry.index < proof_parent_.size()) {
        proof_parent_[entry.index] = entry.flat_value;
        proof_parent_label_endpoint_[entry.index] = entry.flat_value2;
        proof_parent_edge_[entry.index] = entry.size_value;
      }
      return;
  }
}

bool FlatCongruenceClosure::check_disequality(const Disequality& disequality) {
  if (conflict_.has_value()) {
    return true;
  }
  ++stats_.disequality_checks;
  if (representative(disequality.lhs) != representative(disequality.rhs)) {
    return false;
  }
  std::vector<sat::Lit> reason;
  begin_reason_collection(reason);
  if (conflict_trace_) {
    auto& state = *conflict_trace_;
    state.current.visited_steps = 0;
    state.current.prefix_steps = 0;
    state.current.duplicate_steps = 0;
    state.current.budget_full_visits = 0;
    state.current.steps.clear();
    if (++state.generation == 0) {
      for (auto& slot : state.seen) slot.generation = 0;
      ++state.generation;
    }
  }
  ConflictTraceScope trace_scope(conflict_trace_ ? &conflict_trace_->collecting : nullptr);
  if (!explain_equal_into(disequality.lhs, disequality.rhs, reason, edges_.size())) {
    throw std::logic_error("flat congruence closure failed to explain disequality conflict");
  }
  append_unique_reason(reason, disequality.reason);
  stats_.explain_length += reason.size();
  record_conflict_reason_length(reason.size());
  conflict_ = std::move(reason);
  ++stats_.conflicts;
  return true;
}

void FlatCongruenceClosure::check_disequalities_touching(FlatTermId lhs_repr, FlatTermId rhs_repr) {
  if (conflict_.has_value()) {
    return;
  }
  const std::vector<std::size_t>& lhs_candidates = disequality_lists_[lhs_repr];
  const std::vector<std::size_t>& rhs_candidates = disequality_lists_[rhs_repr];
  std::size_t lhs_index = 0;
  std::size_t rhs_index = 0;
  while (lhs_index < lhs_candidates.size() && rhs_index < rhs_candidates.size()) {
    if (lhs_candidates[lhs_index] < rhs_candidates[rhs_index]) {
      ++lhs_index;
      continue;
    }
    if (rhs_candidates[rhs_index] < lhs_candidates[lhs_index]) {
      ++rhs_index;
      continue;
    }
    const std::size_t disequality_index = lhs_candidates[lhs_index];
    ++lhs_index;
    ++rhs_index;
    if (check_disequality(disequalities_[disequality_index])) {
      return;
    }
  }
}

std::optional<std::vector<sat::Lit>> FlatCongruenceClosure::explain_equal(FlatTermId lhs,
                                                                          FlatTermId rhs) const {
  std::vector<sat::Lit> reason;
  begin_reason_collection(reason);
  if (!explain_equal_into(lhs, rhs, reason, edges_.size())) {
    return std::nullopt;
  }
  return reason;
}

void FlatCongruenceClosure::begin_reason_collection(std::vector<sat::Lit>& reason) const {
  reason.clear();
  explanation_cache_.clear();
  explanation_depth_ = 0;
  ++reason_generation_;
  if (reason_generation_ == 0) {
    std::fill(reason_marks_.begin(), reason_marks_.end(), 0);
    ++reason_generation_;
  }
}

void FlatCongruenceClosure::append_unique_reason(std::vector<sat::Lit>& target,
                                                 const std::vector<sat::Lit>& source) const {
  for (const sat::Lit lit : source) {
    const std::size_t index = lit_index(lit);
    if (index >= reason_marks_.size()) {
      reason_marks_.resize(index + 1, 0);
    }
    if (reason_marks_[index] == reason_generation_) {
      ++stats_.explain_reason_duplicate_literals;
      continue;
    }
    reason_marks_[index] = reason_generation_;
    target.push_back(lit);
  }
}

void FlatCongruenceClosure::record_conflict_reason_length(std::size_t length) {
  stats_.conflict_reason_max_length = std::max(stats_.conflict_reason_max_length, length);
  if (length <= 4) {
    ++stats_.conflict_reason_length_le_4;
  } else if (length <= 8) {
    ++stats_.conflict_reason_length_le_8;
  } else if (length <= 16) {
    ++stats_.conflict_reason_length_le_16;
  } else if (length <= 32) {
    ++stats_.conflict_reason_length_le_32;
  } else if (length <= 64) {
    ++stats_.conflict_reason_length_le_64;
  } else {
    ++stats_.conflict_reason_length_gt_64;
  }
}

FlatCongruenceClosure::ExplanationKey FlatCongruenceClosure::explanation_key(FlatTermId lhs,
                                                                             FlatTermId rhs) const {
  if (rhs < lhs) {
    std::swap(lhs, rhs);
  }
  return ExplanationKey{lhs, rhs};
}

bool FlatCongruenceClosure::append_edge_reason(std::size_t edge_index,
                                               std::vector<sat::Lit>& reason) const {
  const Edge& edge = edges_[edge_index];
  if (edge.congruence) {
    trace_conflict_congruence(edge);
    ++stats_.explain_congruence_edges;
    return explain_equal_into(edge.lhs_definition.function, edge.rhs_definition.function, reason,
                              edge_index) &&
           explain_equal_into(edge.lhs_definition.argument, edge.rhs_definition.argument, reason,
                              edge_index);
  }
  ++stats_.explain_direct_edges;
  append_unique_reason(reason, edge.reason);
  return true;
}

void FlatCongruenceClosure::record_proof_parent(FlatTermId from, FlatTermId to,
                                                FlatTermId label_endpoint, std::size_t edge_index) {
  if (from >= proof_parent_.size() || to >= proof_parent_.size()) {
    return;
  }
  if (!checkpoints_.empty()) {
    TrailEntry entry;
    entry.kind = TrailKind::ProofParent;
    entry.index = from;
    entry.flat_value = proof_parent_[from];
    entry.flat_value2 = proof_parent_label_endpoint_[from];
    entry.size_value = proof_parent_edge_[from];
    trail_.push_back(std::move(entry));
    ++stats_.trail_entries;
  }
  proof_parent_[from] = to;
  proof_parent_label_endpoint_[from] = label_endpoint;
  proof_parent_edge_[from] = edge_index;
}

FlatTermId FlatCongruenceClosure::proof_explain_find(FlatTermId term) const {
  if (term >= proof_explain_parent_.size()) {
    return term;
  }
  FlatTermId root = term;
  while (proof_explain_parent_[root] != root) {
    root = proof_explain_parent_[root];
  }
  while (proof_explain_parent_[term] != term) {
    const FlatTermId next = proof_explain_parent_[term];
    proof_explain_parent_[term] = root;
    term = next;
  }
  return root;
}

FlatTermId FlatCongruenceClosure::proof_explain_highest(FlatTermId term) const {
  if (term >= proof_explain_highest_.size()) {
    return term;
  }
  return proof_explain_highest_[proof_explain_find(term)];
}

void FlatCongruenceClosure::proof_explain_union_to_parent(FlatTermId child,
                                                          FlatTermId parent) const {
  if (child >= proof_explain_parent_.size() || parent >= proof_explain_parent_.size()) {
    return;
  }
  const FlatTermId child_root = proof_explain_find(child);
  const FlatTermId parent_root = proof_explain_find(parent);
  if (child_root == parent_root) {
    return;
  }
  proof_explain_parent_[child_root] = parent_root;
}

std::optional<FlatTermId> FlatCongruenceClosure::proof_forest_nearest_common_ancestor(
    FlatTermId lhs, FlatTermId rhs, std::size_t max_edge_index) const {
  ++stats_.explain_proof_forest_queries;
  if (proof_lca_marks_.size() < proof_parent_.size()) {
    proof_lca_marks_.resize(proof_parent_.size(), 0);
    proof_lca_depths_.resize(proof_parent_.size(), 0);
  }
  ++proof_lca_generation_;
  if (proof_lca_generation_ == 0) {
    std::fill(proof_lca_marks_.begin(), proof_lca_marks_.end(), 0);
    ++proof_lca_generation_;
  }

  FlatTermId current = proof_explain_highest(lhs);
  while (current != kInvalidFlatTermId && current < proof_parent_.size() &&
         proof_lca_marks_[current] != proof_lca_generation_) {
    proof_lca_marks_[current] = proof_lca_generation_;
    if (proof_parent_edge_[current] >= max_edge_index) {
      break;
    }
    current = proof_explain_highest(proof_parent_[current]);
  }

  current = proof_explain_highest(rhs);
  while (current != kInvalidFlatTermId && current < proof_parent_.size()) {
    if (proof_lca_marks_[current] == proof_lca_generation_) {
      return current;
    }
    if (proof_parent_edge_[current] >= max_edge_index) {
      break;
    }
    current = proof_explain_highest(proof_parent_[current]);
  }
  ++stats_.explain_proof_forest_missing_lca;
  return std::nullopt;
}

void FlatCongruenceClosure::append_proof_edge_label(
    std::size_t edge_index, std::vector<sat::Lit>& reason,
    std::vector<ProofObligation>& pending_proofs) const {
  const Edge& edge = edges_[edge_index];
  if (edge.congruence) {
    trace_conflict_congruence(edge);
    ++stats_.explain_congruence_edges;
    pending_proofs.push_back(
        ProofObligation{edge.lhs_definition.function, edge.rhs_definition.function, edge_index});
    pending_proofs.push_back(
        ProofObligation{edge.lhs_definition.argument, edge.rhs_definition.argument, edge_index});
    return;
  }
  ++stats_.explain_direct_edges;
  append_unique_reason(reason, edge.reason);
}

bool FlatCongruenceClosure::explain_proof_forest_path(
    FlatTermId from, FlatTermId ancestor, std::size_t max_edge_index, std::vector<sat::Lit>& reason,
    std::vector<ProofObligation>& pending_proofs) const {
  FlatTermId current = proof_explain_highest(from);
  std::size_t path_edges = 0;
  while (current != ancestor) {
    if (current == kInvalidFlatTermId || current >= proof_parent_.size() ||
        proof_parent_[current] == kInvalidFlatTermId ||
        proof_parent_edge_[current] >= max_edge_index) {
      return false;
    }
    const std::size_t edge_index = proof_parent_edge_[current];
    const FlatTermId parent = proof_parent_[current];
    const FlatTermId label_endpoint = proof_parent_label_endpoint_[current];
    if (label_endpoint != kInvalidFlatTermId && label_endpoint != current) {
      pending_proofs.push_back(ProofObligation{current, label_endpoint, edge_index});
    }
    append_proof_edge_label(edge_index, reason, pending_proofs);
    ++path_edges;
    proof_explain_union_to_parent(current, parent);
    current = proof_explain_highest(parent);
  }
  stats_.explain_proof_forest_path_edges += path_edges;
  stats_.explain_proof_forest_max_path_edges =
      std::max(stats_.explain_proof_forest_max_path_edges, path_edges);
  return true;
}

bool FlatCongruenceClosure::explain_equal_with_proof_forest_into(FlatTermId lhs, FlatTermId rhs,
                                                                 std::vector<sat::Lit>& reason,
                                                                 std::size_t max_edge_index) const {
  proof_explain_parent_.resize(parent_.size());
  proof_explain_highest_.resize(parent_.size());
  for (FlatTermId term = 0; term < parent_.size(); ++term) {
    proof_explain_parent_[term] = term;
    proof_explain_highest_[term] = term;
  }
  proof_obligation_scratch_.clear();
  proof_obligation_scratch_.push_back(ProofObligation{lhs, rhs, max_edge_index});
  bool first_obligation = true;
  while (!proof_obligation_scratch_.empty()) {
    const ProofObligation obligation = proof_obligation_scratch_.back();
    proof_obligation_scratch_.pop_back();
    if (!first_obligation) {
      ++stats_.explain_calls;
      ++stats_.explain_recursive_queries;
    }
    first_obligation = false;
    if (obligation.lhs == obligation.rhs ||
        proof_explain_find(obligation.lhs) == proof_explain_find(obligation.rhs)) {
      ++stats_.explain_repeated_pair_queries;
      continue;
    }
    if (obligation.lhs >= parent_.size() || obligation.rhs >= parent_.size() ||
        representative(obligation.lhs) != representative(obligation.rhs)) {
      return false;
    }
    const std::optional<FlatTermId> ancestor = proof_forest_nearest_common_ancestor(
        obligation.lhs, obligation.rhs, obligation.max_edge_index);
    if (!ancestor.has_value()) {
      return false;
    }
    if (!explain_proof_forest_path(obligation.lhs, ancestor.value(), obligation.max_edge_index,
                                   reason, proof_obligation_scratch_) ||
        !explain_proof_forest_path(obligation.rhs, ancestor.value(), obligation.max_edge_index,
                                   reason, proof_obligation_scratch_)) {
      return false;
    }
  }
  return true;
}

std::optional<std::size_t> FlatCongruenceClosure::proof_forest_path_length(
    FlatTermId lhs, FlatTermId rhs, std::size_t max_edge_index) const {
  if (!proof_forest_diagnostics_ || lhs == rhs || lhs >= proof_parent_.size() ||
      rhs >= proof_parent_.size()) {
    return std::nullopt;
  }
  ++stats_.explain_proof_forest_queries;
  if (proof_lca_marks_.size() < proof_parent_.size()) {
    proof_lca_marks_.resize(proof_parent_.size(), 0);
    proof_lca_depths_.resize(proof_parent_.size(), 0);
  }
  ++proof_lca_generation_;
  if (proof_lca_generation_ == 0) {
    std::fill(proof_lca_marks_.begin(), proof_lca_marks_.end(), 0);
    ++proof_lca_generation_;
  }

  FlatTermId current = lhs;
  std::size_t lhs_depth = 0;
  while (current != kInvalidFlatTermId && current < proof_parent_.size() &&
         proof_lca_marks_[current] != proof_lca_generation_) {
    proof_lca_marks_[current] = proof_lca_generation_;
    proof_lca_depths_[current] = lhs_depth;
    if (proof_parent_edge_[current] >= max_edge_index) {
      break;
    }
    current = proof_parent_[current];
    ++lhs_depth;
  }

  current = rhs;
  std::size_t rhs_depth = 0;
  while (current != kInvalidFlatTermId && current < proof_parent_.size()) {
    if (proof_lca_marks_[current] == proof_lca_generation_) {
      const std::size_t path_edges = proof_lca_depths_[current] + rhs_depth;
      stats_.explain_proof_forest_path_edges += path_edges;
      stats_.explain_proof_forest_max_path_edges =
          std::max(stats_.explain_proof_forest_max_path_edges, path_edges);
      return path_edges;
    }
    if (proof_parent_edge_[current] >= max_edge_index) {
      break;
    }
    current = proof_parent_[current];
    ++rhs_depth;
  }
  ++stats_.explain_proof_forest_missing_lca;
  return std::nullopt;
}

void FlatCongruenceClosure::record_proof_forest_path_comparison(std::size_t proof_path_edges,
                                                                std::size_t bfs_path_edges) const {
  if (proof_path_edges < bfs_path_edges) {
    ++stats_.explain_proof_forest_shorter_than_bfs;
    return;
  }
  if (proof_path_edges == bfs_path_edges) {
    ++stats_.explain_proof_forest_equal_to_bfs;
    return;
  }
  ++stats_.explain_proof_forest_longer_than_bfs;
}

bool FlatCongruenceClosure::explain_equal_via_reoriented_proof_forest_into(
    FlatTermId lhs, FlatTermId rhs, std::vector<sat::Lit>& reason) const {
  const std::optional<std::vector<std::size_t>> result = reoriented_proof_forest_.explain(lhs, rhs);
  if (!result.has_value()) {
    return false;
  }
  for (const std::size_t edge_index : result.value()) {
    append_unique_reason(reason, edges_[edge_index].reason);
  }
  return true;
}

bool FlatCongruenceClosure::explain_equal_via_structural_proof_forest_into(
    FlatTermId lhs, FlatTermId rhs, std::vector<sat::Lit>& reason) const {
  const std::optional<std::vector<std::size_t>> result = structural_proof_forest_.explain(lhs, rhs);
  if (!result.has_value()) {
    return false;
  }
  for (const std::size_t edge_index : result.value()) {
    append_unique_reason(reason, edges_[edge_index].reason);
  }
  return true;
}

bool FlatCongruenceClosure::explain_equal_into(FlatTermId lhs, FlatTermId rhs,
                                               std::vector<sat::Lit>& reason,
                                               std::size_t max_edge_index) const {
  if (lhs == rhs) {
    return true;
  }
  if (lhs >= parent_.size() || rhs >= parent_.size()) {
    return false;
  }
  ++stats_.explain_calls;
  if (representative(lhs) != representative(rhs)) {
    return false;
  }
  const ExplanationKey key = explanation_key(lhs, rhs);
  if (explanation_depth_ == 0) {
    ++stats_.explain_top_level_queries;
  } else {
    ++stats_.explain_recursive_queries;
  }
  if (const auto cached = explanation_cache_.find(key); cached != explanation_cache_.end()) {
    if (cached->second <= max_edge_index) {
      ++stats_.explain_cache_hits;
      ++stats_.explain_repeated_pair_queries;
      return true;
    }
    ++stats_.explain_cache_bound_misses;
  }
  std::optional<std::size_t> proof_path_edges;
  if (proof_forest_diagnostics_) {
    proof_path_edges = proof_forest_path_length(lhs, rhs, max_edge_index);
  }
  stats_.explain_max_recursion_depth =
      std::max(stats_.explain_max_recursion_depth, explanation_depth_);
  if (explanation_search_mode_ == FlatExplanationSearchMode::ProofForest) {
    const bool explained = explain_equal_with_proof_forest_into(lhs, rhs, reason, max_edge_index);
    if (explained) {
      const auto [cached, inserted] = explanation_cache_.emplace(key, max_edge_index);
      if (!inserted && max_edge_index < cached->second) {
        cached->second = max_edge_index;
      }
    }
    return explained;
  }
  if (explanation_search_mode_ == FlatExplanationSearchMode::ReorientedProofForest) {
    const bool explained = explain_equal_via_reoriented_proof_forest_into(lhs, rhs, reason);
    if (explained) {
      const auto [cached, inserted] = explanation_cache_.emplace(key, max_edge_index);
      if (!inserted && max_edge_index < cached->second) {
        cached->second = max_edge_index;
      }
    }
    return explained;
  }
  if (explanation_search_mode_ == FlatExplanationSearchMode::StructuralProofForest) {
    const bool explained = explain_equal_via_structural_proof_forest_into(lhs, rhs, reason);
    if (explained) {
      const auto [cached, inserted] = explanation_cache_.emplace(key, max_edge_index);
      if (!inserted && max_edge_index < cached->second) {
        cached->second = max_edge_index;
      }
    }
    return explained;
  }
  if (visit_marks_.size() < parent_.size()) {
    visit_marks_.resize(parent_.size(), 0);
    parent_edge_scratch_.resize(parent_.size(), edges_.size());
    parent_scratch_.resize(parent_.size(), kInvalidFlatTermId);
  }
  ++stats_.explain_bfs_searches;
  ++visit_generation_;
  if (visit_generation_ == 0) {
    std::fill(visit_marks_.begin(), visit_marks_.end(), 0);
    std::fill(reverse_visit_marks_.begin(), reverse_visit_marks_.end(), 0);
    ++visit_generation_;
  }
  queue_scratch_.clear();
  std::size_t queue_index = 0;
  visit_marks_[lhs] = visit_generation_;
  queue_scratch_.push_back(lhs);
  if (path_edges_scratch_.size() <= explanation_depth_) {
    path_edges_scratch_.resize(explanation_depth_ + 1);
  }
  std::vector<std::size_t>& path_edges = path_edges_scratch_[explanation_depth_++];
  path_edges.clear();

  if (explanation_search_mode_ == FlatExplanationSearchMode::BidirectionalBfs) {
    if (reverse_visit_marks_.size() < parent_.size()) {
      reverse_visit_marks_.resize(parent_.size(), 0);
      reverse_parent_edge_scratch_.resize(parent_.size(), edges_.size());
      reverse_parent_scratch_.resize(parent_.size(), kInvalidFlatTermId);
    }
    reverse_queue_scratch_.clear();
    std::size_t reverse_queue_index = 0;
    reverse_visit_marks_[rhs] = visit_generation_;
    reverse_queue_scratch_.push_back(rhs);
    FlatTermId meeting = kInvalidFlatTermId;

    auto expand_frontier =
        [&](std::vector<FlatTermId>& queue, std::size_t& queue_position,
            std::vector<std::size_t>& own_marks, std::vector<std::size_t>& other_marks,
            std::vector<FlatTermId>& own_parents, std::vector<std::size_t>& own_parent_edges) {
          const FlatTermId current = queue[queue_position++];
          ++stats_.explain_bfs_visited;
          for (const std::size_t edge_index : edge_adjacency_[current]) {
            if (edge_index >= max_edge_index) {
              ++stats_.explain_causal_edge_skips;
              break;
            }
            const Edge& edge = edges_[edge_index];
            FlatTermId next = kInvalidFlatTermId;
            if (edge.lhs == current) {
              next = edge.rhs;
            } else if (edge.rhs == current) {
              next = edge.lhs;
            }
            if (next == kInvalidFlatTermId || next >= own_marks.size() ||
                own_marks[next] == visit_generation_) {
              continue;
            }
            own_marks[next] = visit_generation_;
            own_parents[next] = current;
            own_parent_edges[next] = edge_index;
            if (other_marks[next] == visit_generation_) {
              meeting = next;
              return;
            }
            queue.push_back(next);
          }
        };

    while (meeting == kInvalidFlatTermId && (queue_index < queue_scratch_.size() ||
                                             reverse_queue_index < reverse_queue_scratch_.size())) {
      const std::size_t forward_remaining = queue_scratch_.size() - queue_index;
      const std::size_t reverse_remaining = reverse_queue_scratch_.size() - reverse_queue_index;
      if (reverse_remaining == 0 ||
          (forward_remaining != 0 && forward_remaining <= reverse_remaining)) {
        expand_frontier(queue_scratch_, queue_index, visit_marks_, reverse_visit_marks_,
                        parent_scratch_, parent_edge_scratch_);
      } else {
        expand_frontier(reverse_queue_scratch_, reverse_queue_index, reverse_visit_marks_,
                        visit_marks_, reverse_parent_scratch_, reverse_parent_edge_scratch_);
      }
    }
    stats_.explain_bfs_max_queue = std::max(stats_.explain_bfs_max_queue,
                                            queue_scratch_.size() + reverse_queue_scratch_.size());

    if (meeting == kInvalidFlatTermId) {
      --explanation_depth_;
      return false;
    }
    for (FlatTermId current = meeting; current != lhs; current = parent_scratch_[current]) {
      path_edges.push_back(parent_edge_scratch_[current]);
    }
    for (FlatTermId current = meeting; current != rhs; current = reverse_parent_scratch_[current]) {
      path_edges.push_back(reverse_parent_edge_scratch_[current]);
    }
  } else {
    while (queue_index < queue_scratch_.size() && visit_marks_[rhs] != visit_generation_) {
      const FlatTermId current = queue_scratch_[queue_index++];
      ++stats_.explain_bfs_visited;
      for (const std::size_t edge_index : edge_adjacency_[current]) {
        if (edge_index >= max_edge_index) {
          ++stats_.explain_causal_edge_skips;
          break;
        }
        const Edge& edge = edges_[edge_index];
        FlatTermId next = kInvalidFlatTermId;
        if (edge.lhs == current) {
          next = edge.rhs;
        } else if (edge.rhs == current) {
          next = edge.lhs;
        }
        if (next == kInvalidFlatTermId || next >= visit_marks_.size() ||
            visit_marks_[next] == visit_generation_) {
          continue;
        }
        visit_marks_[next] = visit_generation_;
        parent_scratch_[next] = current;
        parent_edge_scratch_[next] = edge_index;
        queue_scratch_.push_back(next);
      }
    }
    stats_.explain_bfs_max_queue = std::max(stats_.explain_bfs_max_queue, queue_scratch_.size());

    if (visit_marks_[rhs] != visit_generation_) {
      --explanation_depth_;
      return false;
    }
    for (FlatTermId current = rhs; current != lhs; current = parent_scratch_[current]) {
      path_edges.push_back(parent_edge_scratch_[current]);
    }
  }
  stats_.explain_bfs_path_edges += path_edges.size();
  for (const std::size_t edge_index : path_edges) {
    if (!append_edge_reason(edge_index, reason)) {
      --explanation_depth_;
      return false;
    }
  }
  if (proof_path_edges.has_value()) {
    record_proof_forest_path_comparison(proof_path_edges.value(), path_edges.size());
  }
  const auto [cached, inserted] = explanation_cache_.emplace(key, max_edge_index);
  if (!inserted && max_edge_index < cached->second) {
    cached->second = max_edge_index;
  }
  --explanation_depth_;
  return true;
}

}  // namespace primo::theory::equality
