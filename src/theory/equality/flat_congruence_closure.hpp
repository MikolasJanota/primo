#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

#include "sat/lit.hpp"
#include "theory/equality/congruence_stats.hpp"
#include "theory/equality/flat_terms.hpp"
#include "theory/equality/proof_forest.hpp"

namespace primo::theory::equality {

class FlatCongruenceClosureTestAccess;

struct ConflictCongruenceTrace {
  std::vector<FlatEquality> steps;
  std::size_t visited_steps = 0;
  std::size_t prefix_steps = 0;
  std::size_t duplicate_steps = 0;
  std::size_t budget_full_visits = 0;
};

enum class FlatExplanationSearchMode {
  OneSidedBfs,
  BidirectionalBfs,
  ProofForest,
  // Nieuwenhuis and Oliveras Section 4/5.2, via the standalone ProofForest
  // class: proof edges link the raw merge endpoints (not representatives)
  // and are reoriented at every union, unlike `ProofForest` above, which
  // links via representatives and was rejected for producing oversized
  // conflict reasons. See docs/papers/fast-congruence-closure.md.
  ReorientedProofForest,
  // Section 5.5's structural-class refinement on top of the above: a
  // congruence merge coalesces the newly discovered structural-class member
  // into its class's existing canonical member (`ProofForest::
  // coalesce_into_structural_class`) instead of adding a tree edge; direct
  // (non-congruence) equation endpoints are resolved to their structural
  // class's tree identity before the ordinary Section 4 Union. See
  // docs/papers/fast-congruence-closure.md.
  StructuralProofForest,
};

class FlatCongruenceClosure {
 public:
  explicit FlatCongruenceClosure(
      std::size_t node_count = 0,
      FlatExplanationSearchMode explanation_search_mode = FlatExplanationSearchMode::OneSidedBfs,
      bool proof_forest_diagnostics = false);

  std::size_t push();
  void pop(std::size_t checkpoint);
  void protect_checkpoint(std::size_t checkpoint);
  void release_protected_checkpoint(std::size_t checkpoint);
  void ensure_node_count(std::size_t node_count);
  void add_definition(const FlatDefinition& definition);
  void assert_equal(FlatTermId lhs, FlatTermId rhs, std::vector<sat::Lit> reason = {});
  void assert_distinct(FlatTermId lhs, FlatTermId rhs, std::vector<sat::Lit> reason = {});
  bool are_equal(FlatTermId lhs, FlatTermId rhs);
  // Expected-linear projection over the eager class representatives maintained
  // by the Nieuwenhuis-Oliveras Section 3 congruence-closure structure.
  std::size_t class_count(const std::vector<FlatTermId>& terms) const;
  std::vector<std::vector<FlatTermId>> equivalence_classes(
      const std::vector<FlatTermId>& terms) const;
  std::optional<std::vector<sat::Lit>> explain_equal(FlatTermId lhs, FlatTermId rhs) const;
  std::optional<std::vector<sat::Lit>> conflict() const { return conflict_; }
  // Dutertre/de Moura (2006), Section 2 p.3: select congruence steps from
  // conflict explanations. Bounded observation only; it never changes reasons.
  void enable_conflict_congruence_trace(std::size_t limit = 64);
  const ConflictCongruenceTrace* conflict_congruence_trace() const;
  // Prepared semantic metadata, never borrowed from the term/SAT layers.
  void set_conflict_trace_semantic_results(std::vector<unsigned char> complete_results);
  void drain_changed_terms(std::vector<FlatTermId>& out);
  const IncrementalCongruenceClosureStats& stats() const { return stats_; }
  std::size_t checkpoint_depth() const { return checkpoints_.size(); }

 private:
  // Section-3-style congruence closure state from Nieuwenhuis and Oliveras:
  // eager representatives, class/use lists, signature lookup, and pending merges.
  struct ApplyKey {
    FlatTermId function = kInvalidFlatTermId;
    FlatTermId argument = kInvalidFlatTermId;

    bool operator==(const ApplyKey& other) const = default;
  };

  struct ApplyKeyHash {
    std::size_t operator()(const ApplyKey& key) const;
  };

  struct ExplanationKey {
    FlatTermId lhs = kInvalidFlatTermId;
    FlatTermId rhs = kInvalidFlatTermId;

    bool operator==(const ExplanationKey& other) const = default;
  };

  struct ExplanationKeyHash {
    std::size_t operator()(const ExplanationKey& key) const;
  };

  struct Definition {
    FlatTermId function = kInvalidFlatTermId;
    FlatTermId argument = kInvalidFlatTermId;
    FlatTermId result = kInvalidFlatTermId;
  };

  struct PendingMerge {
    FlatTermId lhs = kInvalidFlatTermId;
    FlatTermId rhs = kInvalidFlatTermId;
    std::vector<sat::Lit> reason;
    bool congruence = false;
    Definition lhs_definition;
    Definition rhs_definition;
  };

  struct Disequality {
    FlatTermId lhs = kInvalidFlatTermId;
    FlatTermId rhs = kInvalidFlatTermId;
    std::vector<sat::Lit> reason;
  };

  struct Edge {
    FlatTermId lhs = kInvalidFlatTermId;
    FlatTermId rhs = kInvalidFlatTermId;
    std::vector<sat::Lit> reason;
    bool congruence = false;
    Definition lhs_definition;
    Definition rhs_definition;
  };

  enum class TrailKind {
    ClassSize,
    ClassMembersMerge,
    UseListRestore,
    UseListPop,
    DisequalityListRestore,
    DisequalityListPop,
    EdgeAdjacencyPop,
    LookupInsertion,
    DefinitionByResultInsertion,
    ProofParent,
  };

  struct TrailEntry {
    TrailKind kind = TrailKind::ClassSize;
    std::size_t index = 0;
    FlatTermId flat_value = kInvalidFlatTermId;
    FlatTermId flat_value2 = kInvalidFlatTermId;
    std::size_t size_value = 0;
    std::size_t payload_index = 0;
    ApplyKey lookup_key;
  };

  struct ProofObligation {
    FlatTermId lhs = kInvalidFlatTermId;
    FlatTermId rhs = kInvalidFlatTermId;
    std::size_t max_edge_index = 0;
  };

  struct Checkpoint {
    std::size_t node_count = 0;
    std::size_t definitions_size = 0;
    std::size_t pending_size = 0;
    std::size_t disequalities_size = 0;
    std::size_t edges_size = 0;
    std::size_t changed_terms_size = 0;
    std::uint64_t changed_terms_generation = 0;
    std::size_t trail_size = 0;
    std::size_t flat_vector_trail_size = 0;
    std::size_t size_vector_trail_size = 0;
    std::optional<std::vector<sat::Lit>> conflict;
    IncrementalCongruenceClosureStats stats;
  };

  FlatTermId representative(FlatTermId term) const;
  ApplyKey signature(const Definition& definition) const;
  void enqueue(FlatTermId lhs, FlatTermId rhs, std::vector<sat::Lit> reason, bool congruence);
  void enqueue_congruence(const Definition& lhs, const Definition& rhs);
  void propagate();
  void merge_classes(FlatTermId lhs, FlatTermId rhs, std::vector<sat::Lit> reason, bool congruence);
  void merge_classes(PendingMerge pending);
  void process_use_list(FlatTermId old_repr, FlatTermId new_repr);
  void append_use_tracked(FlatTermId representative, FlatTermId result);
  void push_class_size_change(FlatTermId term);
  void restore_trail_entry(TrailEntry& entry);
  bool check_disequality(const Disequality& disequality);
  void check_disequalities_touching(FlatTermId lhs_repr, FlatTermId rhs_repr);
  void begin_reason_collection(std::vector<sat::Lit>& reason) const;
  void append_unique_reason(std::vector<sat::Lit>& target,
                            const std::vector<sat::Lit>& source) const;
  void record_conflict_reason_length(std::size_t length);
  ExplanationKey explanation_key(FlatTermId lhs, FlatTermId rhs) const;
  bool append_edge_reason(std::size_t edge_index, std::vector<sat::Lit>& reason) const;
  void trace_conflict_congruence(const Edge& edge) const;
  bool explain_equal_into(FlatTermId lhs, FlatTermId rhs, std::vector<sat::Lit>& reason,
                          std::size_t max_edge_index) const;
  bool explain_equal_with_proof_forest_into(FlatTermId lhs, FlatTermId rhs,
                                            std::vector<sat::Lit>& reason,
                                            std::size_t max_edge_index) const;
  void append_proof_edge_label(std::size_t edge_index, std::vector<sat::Lit>& reason,
                               std::vector<ProofObligation>& pending_proofs) const;
  FlatTermId proof_explain_find(FlatTermId term) const;
  FlatTermId proof_explain_highest(FlatTermId term) const;
  void proof_explain_union_to_parent(FlatTermId child, FlatTermId parent) const;
  std::optional<FlatTermId> proof_forest_nearest_common_ancestor(FlatTermId lhs, FlatTermId rhs,
                                                                 std::size_t max_edge_index) const;
  bool explain_proof_forest_path(FlatTermId from, FlatTermId ancestor, std::size_t max_edge_index,
                                 std::vector<sat::Lit>& reason,
                                 std::vector<ProofObligation>& pending_proofs) const;
  void record_proof_parent(FlatTermId from, FlatTermId to, FlatTermId label_endpoint,
                           std::size_t edge_index);
  std::optional<std::size_t> proof_forest_path_length(FlatTermId lhs, FlatTermId rhs,
                                                      std::size_t max_edge_index) const;
  void record_proof_forest_path_comparison(std::size_t proof_path_edges,
                                           std::size_t bfs_path_edges) const;
  bool explain_equal_via_reoriented_proof_forest_into(FlatTermId lhs, FlatTermId rhs,
                                                      std::vector<sat::Lit>& reason) const;
  bool explain_equal_via_structural_proof_forest_into(FlatTermId lhs, FlatTermId rhs,
                                                      std::vector<sat::Lit>& reason) const;

  std::vector<FlatTermId> parent_;
  struct ConflictTraceState {
    std::size_t limit = 0;
    bool collecting = false;
    struct SeenPair {
      FlatTermId lhs = 0;
      FlatTermId rhs = 0;
      std::size_t generation = 0;
    };
    std::vector<SeenPair> seen;
    std::size_t generation = 0;
    std::vector<unsigned char> semantic_results;
    ConflictCongruenceTrace current;
    std::vector<ConflictCongruenceTrace> checkpoints;
  };
  mutable std::optional<ConflictTraceState> conflict_trace_;
  std::vector<std::size_t> class_size_;
  std::vector<std::vector<FlatTermId>> class_members_;
  std::vector<std::vector<FlatTermId>> use_lists_;
  std::vector<Definition> definitions_;
  std::unordered_map<FlatTermId, std::size_t> definition_by_result_;
  std::unordered_map<ApplyKey, FlatTermId, ApplyKeyHash> lookup_;
  std::vector<PendingMerge> pending_;
  std::vector<Disequality> disequalities_;
  std::vector<std::vector<std::size_t>> disequality_lists_;
  std::vector<Edge> edges_;
  std::vector<FlatTermId> proof_parent_;
  std::vector<FlatTermId> proof_parent_label_endpoint_;
  std::vector<std::size_t> proof_parent_edge_;
  ProofForest reoriented_proof_forest_;
  ProofForest structural_proof_forest_;
  std::vector<FlatTermId> changed_terms_;
  // A drain consumes notifications from every open checkpoint. Recording that
  // fact as one generation change keeps draining independent of SAT depth;
  // pop uses the saved generation to decide whether an old vector prefix is
  // still live. On saturation the counter rebases and every open checkpoint is
  // rewritten to an empty prefix at the new live generation, because no
  // sentinel generation would stay distinct from the counter forever.
  std::uint64_t changed_terms_generation_ = 0;
  std::vector<std::vector<std::size_t>> edge_adjacency_;
  std::vector<Checkpoint> checkpoints_;
  std::optional<std::size_t> protected_checkpoint_;
  std::vector<TrailEntry> trail_;
  std::vector<std::vector<FlatTermId>> flat_vector_trail_;
  std::vector<std::vector<std::size_t>> size_vector_trail_;
  std::optional<std::vector<sat::Lit>> conflict_;
  mutable IncrementalCongruenceClosureStats stats_;
  mutable std::vector<std::size_t> visit_marks_;
  mutable std::vector<std::size_t> reverse_visit_marks_;
  mutable std::vector<std::size_t> parent_edge_scratch_;
  mutable std::vector<std::size_t> reverse_parent_edge_scratch_;
  mutable std::vector<FlatTermId> parent_scratch_;
  mutable std::vector<FlatTermId> reverse_parent_scratch_;
  mutable std::vector<FlatTermId> queue_scratch_;
  mutable std::vector<FlatTermId> reverse_queue_scratch_;
  mutable std::vector<std::vector<std::size_t>> path_edges_scratch_;
  mutable std::vector<std::size_t> proof_lca_marks_;
  mutable std::vector<std::size_t> proof_lca_depths_;
  mutable std::size_t proof_lca_generation_ = 0;
  mutable std::vector<FlatTermId> proof_explain_parent_;
  mutable std::vector<FlatTermId> proof_explain_highest_;
  mutable std::vector<ProofObligation> proof_obligation_scratch_;
  mutable std::unordered_map<ExplanationKey, std::size_t, ExplanationKeyHash> explanation_cache_;
  mutable std::size_t explanation_depth_ = 0;
  mutable std::size_t visit_generation_ = 0;
  mutable std::vector<std::size_t> reason_marks_;
  mutable std::size_t reason_generation_ = 0;
  std::vector<std::size_t> use_list_marks_;
  std::size_t use_list_generation_ = 0;
  FlatExplanationSearchMode explanation_search_mode_ = FlatExplanationSearchMode::OneSidedBfs;
  bool proof_forest_diagnostics_ = false;

  friend class FlatCongruenceClosureTestAccess;
};

}  // namespace primo::theory::equality
