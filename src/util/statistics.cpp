#include "util/statistics.hpp"

#include <array>
#include <cstddef>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace primo::util {

namespace {

using namespace std::string_view_literals;

constexpr std::array<std::string_view, static_cast<std::size_t>(StatKey::Count)> kStatNames = {
    ":input.assertions"sv,
    ":preprocessing.ite.boolean"sv,
    ":preprocessing.ite.auxiliaries"sv,
    ":preprocessing.assertions"sv,
    ":preprocessing.passes"sv,
    ":preprocessing.changed-passes"sv,
    ":preprocessing.adaptive-passes"sv,
    ":preprocessing.term-size.initial"sv,
    ":preprocessing.term-size.final"sv,
    ":preprocessing.eq-bridge.equalities"sv,
    ":preprocessing.eq-bridge.skipped-or-nodes"sv,
    ":preprocessing.boolean-units.facts"sv,
    ":preprocessing.boolean-units.rewrites"sv,
    ":preprocessing.boolean-units.memo-hits"sv,
    ":preprocessing.boolean-units.memo-misses"sv,
    ":preprocessing.forced-atoms.facts"sv,
    ":preprocessing.forced-atoms.disequalities"sv,
    ":preprocessing.forced-atoms.rewrites"sv,
    ":preprocessing.forced-atoms.memo-hits"sv,
    ":preprocessing.forced-atoms.memo-misses"sv,
    ":preprocessing.unit-equality.consequences"sv,
    ":preprocessing.unit-equality.substitution-units"sv,
    ":preprocessing.unit-equality.substitutions"sv,
    ":preprocessing.unit-equality.substitution-accepted"sv,
    ":preprocessing.unit-equality.substitution-rejected"sv,
    ":preprocessing.equality-query.nodes"sv,
    ":preprocessing.equality-query.memo-hits"sv,
    ":preprocessing.equality-query.add-term-requests"sv,
    ":preprocessing.equality-query.add-term-seen-hits"sv,
    ":preprocessing.solve-eqs.candidates"sv,
    ":preprocessing.solve-eqs.accepted"sv,
    ":preprocessing.solve-eqs.rewrites"sv,
    ":preprocessing.solve-eqs.rejected-cycles"sv,
    ":preprocessing.solve-eqs.rejected-blowups"sv,
    ":preprocessing.finite-domain.domains"sv,
    ":preprocessing.finite-domain.amo-clauses"sv,
    ":preprocessing.finite-domain.raw-choices"sv,
    ":preprocessing.finite-domain.raw-amo-clauses"sv,
    ":preprocessing.finite-domain.post-amo-clauses"sv,
    ":preprocessing.finite-domain.deduped-amo-clauses"sv,
    ":preprocessing.finite-domain.evaluated-equalities"sv,
    ":preprocessing.finite-domain.evaluation-cutoffs"sv,
    ":preprocessing.equality-definitions"sv,
    ":preprocessing.equality-definitions.candidate-pairs"sv,
    ":preprocessing.equality-definitions.accepted-pairs"sv,
    ":preprocessing.equality-definitions.predicted-clauses"sv,
    ":preprocessing.equality-definitions.rejected-domain-size"sv,
    ":preprocessing.equality-definitions.rejected-clause-limit"sv,
    ":preprocessing.ground-congruence-clauses"sv,
    ":preprocessing.ground-congruence-skipped-groups"sv,
    ":preprocessing.uf-symmetry.accepted-sets"sv,
    ":preprocessing.uf-symmetry.values"sv,
    ":preprocessing.uf-symmetry.terms"sv,
    ":preprocessing.uf-symmetry.constraints"sv,
    ":preprocessing.uf-symmetry.value-precedence-terms"sv,
    ":preprocessing.uf-symmetry.value-precedence-constraints"sv,
    ":preprocessing.uf-symmetry.rejected-noninvariant"sv,
    ":preprocessing.auto.single-assertion"sv,
    ":preprocessing.auto.strong"sv,
    ":preprocessing.auto.basic-single"sv,
    ":preprocessing.auto.basic-multi"sv,
    ":preprocessing.auto.large-assertion-skip"sv,
    ":sat.phase.theory-negative"sv,
    ":sat.phase.auto-polarity"sv,
    ":sat.phase.auto-basic-single-predicate-polarity"sv,
    ":sat.phase.auto-basic-multi-polarity"sv,
    ":sat.vars"sv,
    ":sat.clauses"sv,
    ":sat.clause-lits"sv,
    ":sat.unit-clauses"sv,
    ":sat.binary-clauses"sv,
    ":sat.conflicts"sv,
    ":sat.decisions"sv,
    ":sat.propagations"sv,
    ":solver.boolean"sv,
    ":solver.qf-uf"sv,
    ":sat.solve-calls"sv,
    ":euf.theory-atoms"sv,
    ":euf.predicate-atoms"sv,
    ":euf.pure-filter.enabled"sv,
    ":euf.pure-atoms.positive"sv,
    ":euf.pure-atoms.negative"sv,
    ":euf.pure-literals.filtered"sv,
    ":euf.assignments"sv,
    ":euf.eq-assignments"sv,
    ":euf.diseq-assignments"sv,
    ":euf.checks"sv,
    ":euf.theory-conflicts"sv,
    ":theory.conflicts"sv,
    ":euf.propagation.rounds"sv,
    ":euf.propagation.candidates"sv,
    ":euf.propagation.delivered"sv,
    ":euf.propagation.reason-lits"sv,
    ":euf.propagation.budget"sv,
    ":euf.propagation.delivery-budget"sv,
    ":euf.propagation.budget-cutoffs"sv,
    ":euf.propagation.hit-rate-cutoffs"sv,
    ":euf.propagation.auto-online"sv,
    ":euf.propagation.auto-conflict"sv,
    ":euf.propagation.auto-off"sv,
    ":euf.incremental.terms"sv,
    ":euf.incremental.flat-definitions"sv,
    ":euf.incremental.merges"sv,
    ":euf.incremental.pending-pops"sv,
    ":euf.incremental.lookup-probes"sv,
    ":euf.incremental.lookup-hits"sv,
    ":euf.incremental.congruence-merges"sv,
    ":euf.incremental.disequality-checks"sv,
    ":euf.incremental.conflicts"sv,
    ":euf.incremental.explain-calls"sv,
    ":euf.incremental.explain-cache-hits"sv,
    ":euf.incremental.explain-cache-bound-misses"sv,
    ":euf.incremental.explain-length"sv,
    ":euf.incremental.explain-top-level-queries"sv,
    ":euf.incremental.explain-recursive-queries"sv,
    ":euf.incremental.explain-repeated-pair-queries"sv,
    ":euf.incremental.explain-max-recursion-depth"sv,
    ":euf.incremental.explain-bfs-searches"sv,
    ":euf.incremental.explain-bfs-visited"sv,
    ":euf.incremental.explain-bfs-path-edges"sv,
    ":euf.incremental.explain-bfs-max-queue"sv,
    ":euf.incremental.explain-proof-forest-queries"sv,
    ":euf.incremental.explain-proof-forest-path-edges"sv,
    ":euf.incremental.explain-proof-forest-max-path-edges"sv,
    ":euf.incremental.explain-proof-forest-missing-lca"sv,
    ":euf.incremental.explain-proof-forest.shorter-than-bfs"sv,
    ":euf.incremental.explain-proof-forest.equal-to-bfs"sv,
    ":euf.incremental.explain-proof-forest.longer-than-bfs"sv,
    ":euf.incremental.explain-causal-edge-skips"sv,
    ":euf.incremental.explain-direct-edges"sv,
    ":euf.incremental.explain-congruence-edges"sv,
    ":euf.incremental.explain-reason-duplicate-literals"sv,
    ":euf.incremental.conflict-reason.max-length"sv,
    ":euf.incremental.conflict-reason.length-le-4"sv,
    ":euf.incremental.conflict-reason.length-le-8"sv,
    ":euf.incremental.conflict-reason.length-le-16"sv,
    ":euf.incremental.conflict-reason.length-le-32"sv,
    ":euf.incremental.conflict-reason.length-le-64"sv,
    ":euf.incremental.conflict-reason.length-gt-64"sv,
    ":euf.incremental.backtracks"sv,
    ":euf.incremental.trail-entries"sv,
    ":euf.incremental.class-member-trail-entries-saved"sv,
    ":euf.incremental.use-list-appends"sv,
    ":euf.incremental.use-list-duplicate-skips"sv,
    ":euf.incremental.affected-use-list-dedup"sv,
    ":euf.incremental.max-use-list-size"sv,
    ":euf.incremental.changed-term-drains"sv,
    ":euf.incremental.changed-terms-delivered"sv,
    ":euf.incremental.changed-term-generation-rebases"sv,
    ":euf.incremental.max-checkpoint-depth"sv,
    ":solver.qf-lra"sv,
    ":lra.theory-checks"sv,
    ":lra.theory-conflicts"sv,
    ":lra.theory-propagations"sv,
    ":lra.bland-fallbacks"sv,
    ":preprocessing.arithmetic-canonicalization.atoms-visited"sv,
    ":preprocessing.arithmetic-canonicalization.atoms-rewritten"sv,
    ":preprocessing.arithmetic-canonicalization.comparisons"sv,
    ":preprocessing.arithmetic-canonicalization.equalities"sv,
    ":preprocessing.arithmetic-canonicalization.constant-folds"sv,
    ":preprocessing.arithmetic-canonicalization.orientation-reversals"sv,
    ":preprocessing.arithmetic-canonicalization.aliases-merged"sv,
    ":preprocessing.arithmetic-canonicalization.duplicate-assertions-removed"sv,
    ":preprocessing.arithmetic-canonicalization.terms-created"sv,
    ":preprocessing.arithmetic-canonicalization.catchup-rewrites"sv,
    ":preprocessing.arithmetic-canonicalization.post-purification-rewrites"sv,
    ":preprocessing.real-equality-lowering.lowered"sv,
    ":preprocessing.real-equality-lowering.guarded-distributed"sv,
    ":preprocessing.monotone-elimination.candidates"sv,
    ":preprocessing.monotone-elimination.variables"sv,
    ":preprocessing.monotone-elimination.atoms-folded"sv,
    ":preprocessing.solve-eqs.gaussian-candidates"sv,
    ":lra.row-propagations"sv,
    ":lra.soi-multi-row-conflicts"sv,
    ":lra.soi-minimize-calls"sv,
    ":lra.soi-minimize-shrunk"sv,
    ":lra.soi-conflict-lits"sv,
    ":lra.pivots"sv,
    ":lra.online-check-calls"sv,
    ":lra.bidirectional-row-propagations"sv,
    ":lra.theory-decisions"sv,
    ":lra.model-phase.refreshes"sv,
    ":lra.model-phase.hints"sv,
    ":rational.promotions"sv,
    ":rational.big-operations"sv,
    ":lra.tableau-vars"sv,
    ":lra.rows"sv,
    ":lra.row-nonzeros"sv,
    ":lra.pivot-row-updates"sv,
    ":lra.pivot-term-ops"sv,
    ":lra.fixed-variables"sv,
    ":lra.elimination-pivots"sv,
    ":lia.generalized-lemmas"sv,
    ":lia.generalized-lemma-literals"sv,
    ":lia.coarse-lemma-literals"sv,
    ":lia.generalization-fallbacks"sv,
    ":lia.branch-nodes"sv,
    ":lia.cube-microseconds"sv,
    ":lia.cube-calls"sv,
    ":lia.cube-skipped"sv,
    ":lia.cube-constraints"sv,
    ":lia.cube-fixed-coordinates"sv,
    ":lia.cube-difference-exemptions"sv,
    ":lia.cube-pivots"sv,
    ":lia.cube-centers"sv,
    ":lia.cube-infeasible"sv,
    ":lia.cube-limits"sv,
    ":lia.cube-rejected"sv,
    ":lia.cube-models"sv,
    ":lia.patch-sweeps"sv,
    ":lia.patched-variables"sv,
    ":lia.patch-rounds-fully-integral"sv,
    ":lia.patch-occurrence-entries"sv,
    ":lia.patch-candidates-scored"sv,
    ":lia.patch-affected-rows-inspected"sv,
    ":lia.patch-full-scan-equivalent"sv,
    ":lia.patch-fractional-nonbasic-moves"sv,
    ":lia.patch-fractional-candidates-rejected"sv,
    ":lia.patch-lattice-candidates-scored"sv,
    ":lia.patch-lattice-moves-accepted"sv,
    ":lia.prep-fractional-int-thresholds"sv,
    ":lia.prep-non-integer-int-slack-atoms"sv,
    ":lia.prep-non-integer-int-slacks"sv,
    ":lia.prep-integer-slacks"sv,
    ":no.boolean-domain.registered-terms"sv,
    ":preprocessing.ite.rewrites"sv,
    ":preprocessing.ite.equality-distributed"sv,
    ":preprocessing.extended.arithmetic-rewrites"sv,
    ":preprocessing.lower.div-quotients"sv,
    ":preprocessing.lower.abs-rewrites"sv,
    ":preprocessing.ite.equality-distribution-blocked"sv,
    ":preprocessing.extended.simplify-rounds"sv,
    ":preprocessing.ite.equality-distribution-nested"sv,
    ":model.invented-int-values"sv,
    ":preprocessing.cite.atoms-visited"sv,
    ":preprocessing.cite.atoms-simplified"sv,
    ":preprocessing.cite.branches-collapsed"sv,
};

const std::unordered_map<std::string_view, StatKey>& stat_keys_by_name() {
  static const std::unordered_map<std::string_view, StatKey> keys = [] {
    std::unordered_map<std::string_view, StatKey> result;
    result.reserve(kStatNames.size());
    for (std::size_t i = 0; i < kStatNames.size(); ++i) {
      result.emplace(kStatNames[i], static_cast<StatKey>(i));
    }
    return result;
  }();
  return keys;
}

}  // namespace

std::string_view stat_name(StatKey key) { return kStatNames.at(static_cast<std::size_t>(key)); }

std::optional<StatKey> Statistics::lookup_key(std::string_view name) {
  const auto& keys = stat_keys_by_name();
  const auto found = keys.find(name);
  if (found != keys.end()) {
    return found->second;
  }
  return std::nullopt;
}

std::size_t Statistics::TransparentStringHash::operator()(std::string_view value) const noexcept {
  return std::hash<std::string_view>{}(value);
}

void Statistics::remember_key(StatKey key) {
  const std::size_t slot = index(key);
  if (!seen_[slot]) {
    seen_[slot] = true;
    order_.push_back(key);
  }
}

void Statistics::set(StatKey key, Value value) {
  remember_key(key);
  values_[index(key)] = value;
}

void Statistics::set(std::string_view name, Value value) {
  if (const auto extra = extra_values_.find(name); extra != extra_values_.end()) {
    extra->second = value;
    return;
  }
  if (const std::optional<StatKey> key = lookup_key(name); key.has_value()) {
    set(key.value(), value);
    return;
  }
  const auto [inserted, is_new] = extra_values_.try_emplace(std::string{name}, value);
  if (is_new) {
    extra_order_.push_back(inserted->first);
  }
}

void Statistics::increment(StatKey key, Value delta) {
  remember_key(key);
  values_[index(key)] += delta;
}

void Statistics::increment(std::string_view name, Value delta) {
  if (const auto extra = extra_values_.find(name); extra != extra_values_.end()) {
    extra->second += delta;
    return;
  }
  if (const std::optional<StatKey> key = lookup_key(name); key.has_value()) {
    increment(key.value(), delta);
    return;
  }
  const auto [inserted, is_new] = extra_values_.try_emplace(std::string{name}, delta);
  if (is_new) {
    extra_order_.push_back(inserted->first);
  }
}

Statistics::Value Statistics::get(StatKey key) const { return values_[index(key)]; }

Statistics::Value Statistics::get(std::string_view name) const {
  if (const auto extra = extra_values_.find(name); extra != extra_values_.end()) {
    return extra->second;
  }
  if (const std::optional<StatKey> key = lookup_key(name); key.has_value()) {
    return get(key.value());
  }
  return 0;
}

bool Statistics::contains(StatKey key) const { return seen_[index(key)]; }

bool Statistics::contains(std::string_view name) const {
  if (extra_values_.contains(name)) {
    return true;
  }
  if (const std::optional<StatKey> key = lookup_key(name); key.has_value()) {
    return contains(key.value());
  }
  return false;
}

std::vector<std::string> Statistics::keys() const {
  std::vector<std::string> result;
  result.reserve(order_.size() + extra_order_.size());
  for (const StatKey key : order_) {
    result.emplace_back(stat_name(key));
  }
  result.insert(result.end(), extra_order_.begin(), extra_order_.end());
  return result;
}

void print_statistics(std::ostream& output, const Statistics& statistics) {
  for (const std::string& key : statistics.keys()) {
    output << key << ' ' << statistics.get(key) << '\n';
  }
}

}  // namespace primo::util
