#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <set>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#include "sat/lit.hpp"
#include "util/rational.hpp"

namespace primo::theory::arithmetic {

// An inconclusive private feasibility run; never a satisfiable check result.
class LraCheckInterrupted final : public std::runtime_error {
 public:
  LraCheckInterrupted() : std::runtime_error("LRA feasibility check interrupted") {}
};

using LraVarId = std::size_t;

// A value in the "rational + infinitesimal" domain: q + k*delta for a
// symbolic infinitesimal delta > 0, used to represent strict-inequality
// bounds/assignments exactly without picking a concrete delta (Dutertre &
// de Moura, "A Fast Linear-Arithmetic Solver for DPLL(T)", Section 3.1).
// Ordered lexicographically on (rational, infinitesimal).
struct DeltaRational {
  util::Rational rational{0};
  util::Rational infinitesimal{0};

  static DeltaRational from_rational(util::Rational value) {
    return DeltaRational{std::move(value), util::Rational(0)};
  }

  DeltaRational operator+(const DeltaRational& other) const {
    return DeltaRational{rational + other.rational, infinitesimal + other.infinitesimal};
  }
  DeltaRational operator-(const DeltaRational& other) const {
    return DeltaRational{rational - other.rational, infinitesimal - other.infinitesimal};
  }
  DeltaRational operator-() const { return DeltaRational{-rational, -infinitesimal}; }
  DeltaRational scaled(const util::Rational& factor) const {
    return DeltaRational{rational * factor, infinitesimal * factor};
  }

  bool operator==(const DeltaRational& other) const {
    return rational == other.rational && infinitesimal == other.infinitesimal;
  }
  bool operator!=(const DeltaRational& other) const { return !(*this == other); }
  bool operator<(const DeltaRational& other) const {
    if (rational != other.rational) {
      return rational < other.rational;
    }
    return infinitesimal < other.infinitesimal;
  }
  bool operator<=(const DeltaRational& other) const { return *this < other || *this == other; }
  bool operator>(const DeltaRational& other) const { return other < *this; }
  bool operator>=(const DeltaRational& other) const { return other <= *this; }
};

// A bounded-variable Simplex tableau for QF_LRA, implementing Section 3 of
// Dutertre & de Moura ("A Fast Linear-Arithmetic Solver for DPLL(T)"):
// Assert-Bound, Check/Pivot, and Farkas-certificate-style conflict
// explanation. Bland's-rule variable selection is the paper's own choice and
// is what the termination argument rests on; three faster rules have since
// been added alongside it (see PivotingRule), each falling back to Bland
// selection past a per-call pivot threshold so termination still holds.
//
// Incremental via push()/pop(checkpoint), trail-based (mirroring
// FlatCongruenceClosure's discipline, not full state snapshotting).
//
// **Only bound changes are trailed** (heuristics-note item H9), which is
// the paper's own design
// (technical report SRI-CSL-06-01 Section 3.2.4: "We just need to save the
// value of u_i (l_i) on a stack before it is updated... Backtracking does
// not require to save the successive betas on a stack... Since no pivoting
// or other expensive operation is used, backtracking is very cheap.")
// Neither the assignment nor the basis is restored, and the argument for
// why that is sound has two halves:
//
//   * the assignment satisfies Ax = 0, and every tableau row is a linear
//     combination of A's rows, so it keeps satisfying the row equations
//     across any change of basis; and
//   * pop() only ever *relaxes* bounds, so the invariant that every
//     non-basic variable sits within its bounds (report equation 3.2)
//     survives untouched.
//
// **(3.2) says *within*, not *at*.** Textbook bounded-variable Simplex pins
// every non-basic variable *to* one of its bounds, so that a basis names a
// vertex; the paper's algorithm only requires l_j <= beta(x_j) <= u_j, and
// non-basic variables here routinely sit strictly inside. The initial state
// is the plainest case: every variable is 0 and unbounded. This is a real
// divergence, not a restatement -- it is what freedom_interval() measures
// (a pinned non-basic would have none) and hence what makes the Section 5.2
// model mutation in diversify_shared_model() possible at all.
//
// Note the two schemes are each self-consistent and it is *mixing* them
// that breaks: restoring the assignment but not the basis would leave a
// variable that was basic at snapshot time -- and so unconstrained by
// (3.2) -- now non-basic and possibly outside its bounds. Primo used to
// restore all three (bounds, assignments, pivots) for exactly that reason,
// which was circular: reverting the assignments is what forced reverting
// the pivots.
//
// **PivotingRule::Soi is the exception and still trails everything.** Its
// objective row is explicitly maintained and genuinely basis-dependent --
// it is a sum over the *currently* violated rows, not a linear combination
// of A -- so neither half of the argument above covers it. SOI is an
// extension beyond the paper (FMCAD 2013), so it keeps the pre-2026-07-31
// behaviour rather than being made to fit an argument written for the
// paper's algorithm.
//
// Rows must all be added via add_row() before the first push() in the
// caller's intended usage (build the full tableau once, then push/assert/
// pop as the SAT search proceeds) -- add_row() itself is not trailed.
//
// Operates on abstract dense variable ids (LraVarId), not core::TermId --
// the caller (check_sat.cpp's solve_qf_lra) is responsible for mapping
// problem/slack variables to ids, mirroring how FlatCongruenceClosure
// operates on FlatTermId rather than core::TermId directly.
class LraTableau {
 public:
  // Selects how check()'s Check/Pivot loop picks which out-of-bound basic
  // variable to repair and which non-basic variable enters the basis.
  // Bland is the paper's own choice (smallest-index for both), the only
  // one with a direct termination proof. Dantzig (largest violation /
  // largest |coefficient|) is cheaper per pivot and faster in practice
  // (docs/papers/lra-related-work.md's survey) but has no anti-cycling
  // guarantee on its own -- check() falls back to Bland's-rule selection
  // after enough pivots within one call to still terminate; see check()'s
  // definition for why a finite Dantzig prefix followed by a Bland's-rule
  // suffix is still covered by the paper's termination argument. Soi
  // (competitiveness-roadmap item 4, King/Barrett/Dutertre FMCAD 2013,
  // "Simplex with Sum of Infeasibilities for SMT") replaces the
  // single-row objective with a global one -- minimize the sum of
  // infeasibilities across *every* out-of-bound basic variable at once --
  // giving pivot selection a global progress criterion Bland/Dantzig
  // both lack. See the SOI-specific members/methods below.
  //
  // Sparse (heuristics-note item H3) prices what a pivot *costs* rather than
  // what it achieves: it keeps Dantzig's most-violated leaving choice and
  // picks, among the eligible entering candidates, the one occurring in the
  // fewest rows. pivot() rewrites every row in occurs_in_[entering], merging
  // in a row of length |row(leaving)|, so a pivot's cost and the fill-in it
  // creates are the Markowitz product |occurs_in(entering)| * |row(leaving)|
  // -- and none of Bland, Dantzig or Soi prices either factor. Crucially,
  // once the leaving variable is fixed, *every* eligible entering candidate
  // repairs the same violation by the same amount, so this choice is free
  // with respect to progress: Bland and Dantzig are already picking
  // arbitrarily among equivalent options, and Sparse just picks the cheapest
  // of them. Same anti-cycling story as Dantzig (fallback to Bland selection
  // past a per-call pivot threshold). Source: Monniaux, "On using
  // floating-point computations to help an exact linear arithmetic decision
  // procedure" (CAV 2009), p.8, which sorts by "number of occurrences in
  // equations" for the same reason -- only that sparsity criterion is taken
  // from the paper, not its floating-point preprocessing contribution.
  //
  // Note this rule is the field's standard, not a technique transferred from
  // one paper: King's thesis ("Effective Algorithms for the Satisfiability of
  // Quantifier-Free Formulas Over Linear Real and Integer Arithmetic", NYU
  // 2014) Section 2.2.12 records minimum-column-length entering selection,
  // with a switch to Bland's rule past a finite cap, as what MathSAT,
  // OpenSMT, Yices, Yices 2, z3 and CVC4 all do. The leaving-side knob
  // (set_sparse_leaving) is the part with no counterpart described there --
  // King records Griggio's most-violated rule, which is our Dantzig choice.
  //
  // NOTE: Sparse (with set_sparse_leaving) is the *solver's* default since
  // 2026-07-29; this class still constructs Bland by default, because Bland
  // is the conservative choice for a directly-constructed tableau and the
  // white-box tests rely on it. CheckSatOptions::lra_pivoting_rule is what
  // sets the solver's.
  enum class PivotingRule { Bland, Dantzig, Soi, Sparse };

  // Numeric pricing/anti-cycling knobs, grouped so they can be set in one
  // call and searched as a block -- see docs/tuning-parameters.md. Every
  // default here reproduces the constant the value was hardcoded to before
  // it became a parameter, so an untuned run behaves exactly as before.
  //
  // None of these can change an answer, only how long it takes to reach
  // one: the pricing limit restricts which of several equally-repairing
  // candidates is chosen, and the fallback threshold only moves *when*
  // Bland's rule takes over, which termination needs to happen after
  // finitely many pivots but not after any particular count. Every value
  // in range is therefore sound, including zero.
  // -- SOI conflict minimization strategy -------------------------------
  //
  // Which algorithm shrinks the error set before build_soi_conflict() turns
  // it into a clause, and in which order candidates are offered up for
  // removal. PivotingRule::Soi only; inert under every other rule.
  //
  // Measured on this code (25 instances, SOI forced, 2253 minimization
  // calls): the error set averages 2.83 variables when it has more than one,
  // never exceeded 9, and minimization reduces it to 1.23. So the *algorithm*
  // is irrelevant here -- QuickXplain's O(k + k log(m/k)) against deletion's
  // O(m) is about one predicate test at m = 3. What matters is that at
  // k ~= 1.23 there are usually several equally minimal answers, and which
  // one survives decides the clause.
  //
  // That choice is currently unspecified: QuickXplain's divide-and-conquer
  // decides which elements get tested together, so it cannot be steered.
  // Deletion can, because it drops the elements it visits first -- hence the
  // ordering below is "worst first".
  //
  // Why it is worth steering: build_soi_conflict emits one literal per
  // nonzero entry of the restricted SOI row PLUS one per error variable.
  // Minimizing |S| shrinks only the second term, while the first depends on
  // *which* rows survive and is usually the larger of the two.
  enum class SoiMinimization {
    QuickXplain,  // Junker 2001, the historical default
    Deletion,     // iterative, steerable by SoiMinimizationOrder
    None,         // ablation baseline and a portfolio arm
  };

  // Deletion's visit order. All are computable from tableau state alone --
  // LraTableau has no decision-level or activity information, and plumbing
  // any in would defeat the point of keeping this off the main solver.
  // Ties break by variable id, so every arm stays deterministic.
  enum class SoiMinimizationOrder {
    Id,          // ascending id: reproduces today's order (violated_vars_ is a std::set)
    RowSupport,  // longest row first: sheds the rows that contribute most literals
    Violation,   // largest bound violation first: the "most responsible row" reading
  };

  struct Tuning {
    // How many *eligible* entering candidates PivotingRule::Sparse scores
    // before settling for the best seen so far; 0 means unbounded (score
    // every eligible candidate in the row).
    std::size_t sparse_pricing_candidates = 16;
    // check()'s per-call Bland's-rule fallback point, as
    // `factor * variables + offset` pivots.
    std::size_t bland_fallback_factor = 4;
    std::size_t bland_fallback_offset = 64;
    // How many values inside a variable's freedom interval
    // diversify_shared_model() tries before giving up on that variable. The
    // interval is exact; which value inside it is worth taking is pure
    // heuristic, so this is the knob that decides how hard mutation tries.
    std::size_t model_mutation_candidates = 4;
    // SOI-only; inert under every other pivoting rule.
    SoiMinimization soi_minimization = SoiMinimization::QuickXplain;
    SoiMinimizationOrder soi_minimization_order = SoiMinimizationOrder::Id;
  };

  // Intended to be set before solving starts, like set_sparse_leaving():
  // not trailed, so changing it mid-search would alter selection with no
  // record of having done so.
  void set_tuning(const Tuning& tuning) { tuning_ = tuning; }
  const Tuning& tuning() const { return tuning_; }

  explicit LraTableau(PivotingRule rule = PivotingRule::Bland) : rule_(rule) {
    if (rule_ == PivotingRule::Soi) {
      // The objective row (Section V): a real extra variable/row so
      // pivot()'s existing, already-correct substitution logic keeps it
      // consistent across basis changes for free -- only the d_i-flip-
      // driven additive update (set_soi_direction()) is genuinely new.
      // Empty initially: no variable is in error before any bounds are
      // asserted, so every d_i starts at 0.
      f_var_ = add_variable();
      add_row(*f_var_, {});
    }
  }

  // A basic variable's row: basic_var = sum(coefficient * var), one
  // (var, coefficient) pair per term.
  //
  // **Invariant: strictly ascending by var id, and never a zero
  // coefficient** (heuristics-note item H8). Held by every producer of a Row -- add_row() sorts,
  // pivot() builds both of its outputs in order, set_soi_direction()
  // sorts, and undo() restores rows that already satisfied it. Relied on
  // by pivot()'s affected-row loop, which combines two rows with a linear
  // merge; before 2026-07-31 that loop concatenated and re-sorted per
  // affected row, which a gprof profile of the mid-band gap put at 11.6%
  // of runtime on scrambled339450 (see docs/qf-lra-heuristics.md section
  // 6.2a). Anything that constructs a Row must preserve it; the unit tests
  // check it directly rather than trusting comments.
  // A row entry carries a back-index into the column list as well as the
  // coefficient: `occ_slot` is this term's position inside
  // `occurs_in_[var]`. That is what lets pivot() remove an occurrence in
  // O(log row) instead of scanning the column list, since the code already
  // holds the row entry at the point it decides the term is gone -- see
  // docs/open-leads.md lead 3b, and King's thesis Section 2.2.7 for the
  // linked-list form this is an array variant of.
  //
  // Rebuilding a row moves the entry within the *row*, but leaves
  // `occurs_in_[var]` untouched for a surviving term, so the merge carries
  // `occ_slot` across unchanged. Slots are only rewritten when the column
  // list itself changes.
  // sizeof is 40 against the old pair's 32, because LraVarId is size_t and
  // the 32-bit slot cannot pack into its padding. That 25% growth in row
  // memory is measurable on merge-heavy instances (about +4% on
  // miplib/danoint-166, which merges 1.07G terms); narrowing LraVarId to
  // 32 bits would put the entry back at 32 bytes and is the obvious
  // follow-up, but it is a wider change than this one.
  struct RowEntry {
    LraVarId var = 0;
    util::Rational coeff;
    std::uint32_t occ_slot = 0;
  };
  using Row = std::vector<RowEntry>;

  // Allocates a fresh non-basic variable with assignment 0 and no bounds.
  LraVarId add_variable();

  // Defines a basic variable's row: basic_var = sum(coefficient * var).
  // basic_var must not already have a row. Coefficients of zero are
  // dropped; an empty `terms` is allowed (basic_var's row is the constant
  // 0 -- reachable when two syntactically different atoms normalize to the
  // same polynomial, see lra_atom.cpp). Not trailed -- see the class
  // comment; must be called before the first push().
  void add_row(LraVarId basic_var, const std::vector<std::pair<LraVarId, util::Rational>>& terms);

  // Returns a checkpoint token for the current trail position.
  std::size_t push() const { return trail_.size(); }

  // Undoes every bound/assignment/pivot change made since `checkpoint`
  // (which must have come from an earlier push()), in reverse order.
  void pop(std::size_t checkpoint);

  // Section 3.1's Assert-Bound. Returns a conflict (the reasons of the two
  // directly-contradicting bounds) if the new bound is immediately
  // inconsistent with the existing opposite bound on the same variable;
  // otherwise records the bound (updating the current assignment, and
  // propagating to dependent basic variables, if var is non-basic) and
  // returns nullopt. A bound weaker than the existing one is a silent
  // no-op.
  std::optional<std::vector<sat::Lit>> assert_upper(LraVarId var, DeltaRational value,
                                                    sat::Lit reason);
  std::optional<std::vector<sat::Lit>> assert_lower(LraVarId var, DeltaRational value,
                                                    sat::Lit reason);

  // Section 3.1's Check. Repeatedly pivots to repair out-of-bounds basic
  // variables until either every basic variable is within its bounds
  // (returns nullopt -- satisfiable, see assignment()) or some basic
  // variable is out of bounds with no repairing pivot available (returns
  // its Farkas-style conflict certificate, Section 3.2).
  std::optional<std::vector<sat::Lit>> check(const std::function<bool()>& cancelled = {});

  const DeltaRational& assignment(LraVarId var) const { return vars_.at(var).assignment; }

  // -- Nelson-Oppen equality sharing (docs/qf-lra-equality-sharing.md) --
  //
  // Partitions `shared` by its current model value, returning only the classes
  // with more than one member. This is the `M-Propagate` trigger from de Moura
  // and Bjorner's Model-based Theory Combination (doi:10.1016/j.entcs.2008.04.079,
  // Section 5): the theories exchange candidate equalities implied by the
  // *current model*, not by the constraint set, and the combination layer turns
  // each candidate into a case split.
  //
  // Scoped to `shared` on purpose. The combination layer only ever cares about
  // variables occurring in more than one theory, so partitioning the whole
  // tableau would compute a partition of thousands of variables to use a
  // handful of it. Cost is O(|shared|) expected -- hashed on the delta-rational
  // model value and on the variable id -- independent of tableau size.
  //
  // **These are conjectures, not entailments.** Two variables sharing a model
  // value need not be equal in every model; that is exactly why the paper's
  // rule creates a case split rather than a propagation. Anything this returns
  // must be checked or branched on, never asserted. Entailed equalities are a
  // separate obligation with a separate return type, because they carry a
  // reason -- see the note above.
  //
  // Keying on DeltaRational and not on its rational part is load-bearing: a
  // strict bound gives a variable an infinitesimal component, and two variables
  // agreeing rationally while differing in delta are NOT equal in the concrete
  // model the delta encoding stands for. Keying on the rational part alone
  // would hand the combination layer equalities that do not hold.
  //
  // Duplicates in `shared` are ignored; the result contains each variable once.
  std::vector<std::vector<LraVarId>> model_equal_classes(const std::vector<LraVarId>& shared) const;

  // An equality between shared variables that holds in EVERY model of the
  // current bounds, together with the reason literals that force it.
  //
  // Unlike model_equal_classes this is an ENTAILMENT, not a conjecture: the
  // combination layer may propagate it rather than branch on it, which is why
  // it carries a reason and the partition does not.
  //
  // `reasons` follows the codebase-wide convention -- already-negated
  // ("blocking") literals, exactly as build_conflict returns and as
  // cb_add_reason_clause_lit expects, so it needs no translation on the way to
  // the SAT solver. An EMPTY reason set is meaningful and legal: it means the
  // equality follows from the tableau's own row structure with no asserted
  // bound involved, so it holds unconditionally.
  enum class EntailedEqualityRule { FixedEq, EqRow, EqOffsetRows, EqRows };

  struct EntailedEquality {
    LraVarId lhs = 0;
    LraVarId rhs = 0;
    std::vector<sat::Lit> reasons;
    EntailedEqualityRule rule = EntailedEqualityRule::FixedEq;
  };

  // Work counters for the current-model-filtered unit-row producer from
  // Bjorner and Nachmanson, "Arithmetic Solving in Z3", Section 3.2. They are
  // returned to the mixed coordinator instead of coupling the theory layer to
  // util::Statistics.
  struct ModelFilteredEqualityStats {
    std::size_t model_classes = 0;
    std::size_t class_terms = 0;
    std::size_t candidate_pairs = 0;
    std::size_t rows_inspected = 0;
    std::size_t nonzeros_inspected = 0;
    std::size_t fixed_matches = 0;
    std::size_t one_row_matches = 0;
    std::size_t two_row_matches = 0;
    std::size_t nonunit_rejections = 0;
    std::size_t nonfixed_row_rejections = 0;
    std::size_t reason_literals = 0;
  };

  struct ModelFilteredEqualityResult {
    std::vector<EntailedEquality> equalities;
    ModelFilteredEqualityStats stats;
  };

  // Applies the cheap entailment rules from de Moura and Bjorner's
  // Model-based Theory Combination, Section 5.1 (doi:10.1016/j.entcs.2008.04.079,
  // and docs/qf-lra-equality-sharing.md for why we care):
  //
  //   FixedEq   l_i = u_i = l_j = u_j                      ==> x_i = x_j
  //   EqRow     x_i = x_j + P, P fixed, beta(P) = 0        ==> x_i = x_j
  //
  // All four Section 5.1 rules are implemented. They remain deliberately
  // INCOMPLETE: what they miss is what the combination layer must case-split
  // on. For a complete but very expensive reference, see
  // lra_implied_real_equalities in check_sat.cpp, which is used as the test
  // oracle.
  //
  // Emits a SPANNING set, not all pairs: within a group of mutually equal
  // variables each is linked to the group's first member, giving n-1
  // equalities rather than n(n-1)/2. Congruence closure on the other side
  // recovers the rest by transitivity, and a quadratic blow-up here would be
  // paid on every call.
  //
  // Const, and polled at check points -- never hooked into pivot() or
  // assert_*. It is not allocation-free: each candidate row builds two small
  // variable vectors plus a reason vector, so the cost is O(shared candidates
  // x row length), paid per check rather than per pivot. Keeping Nelson-Oppen work off the LRA hot
  // path is deliberate -- see docs/nelson-oppen-qf-lra-regression.md for what happened last time it
  // was not.
  std::vector<EntailedEquality> entailed_equalities(const std::vector<LraVarId>& shared) const;

  // Section 3.2's offset-free producer. Equal current model values are only a
  // candidate filter; the fixed/unit-row shapes establish entailment and
  // provide reasons. Output is a deterministic spanning forest. The method is
  // deliberately separate from entailed_equalities() so the mixed coordinator
  // can A/B it against the legacy exact-offset producer.
  ModelFilteredEqualityResult model_filtered_unit_equalities(
      const std::vector<LraVarId>& shared) const;

  // -- M-Mutate (paper Section 5.2, "Mutation using freedom intervals") ------
  //
  // The range over which a NONBASIC variable's value may be moved while every
  // basic variable stays inside its bounds. `nullopt` on an endpoint means
  // unbounded in that direction. From the paper, for nonbasic x_j:
  //
  //   L_j = max({b(x_j) + (l_i - b(x_i))/a_ij | a_ij > 0}
  //           u {b(x_j) + (u_i - b(x_i))/a_ij | a_ij < 0} u {l_j})
  //   U_j = min({b(x_j) + (u_i - b(x_i))/a_ij | a_ij > 0}
  //           u {b(x_j) + (l_i - b(x_i))/a_ij | a_ij < 0} u {u_j})
  //
  // The point of the interval is that it makes model diversification cost an
  // O(column) computation instead of a solve: any value inside it keeps the
  // assignment a genuine model of the current constraints, so a disequality
  // read off the mutated model is true in some model rather than merely
  // unrefuted. That is what lets the combination layer skip a case split
  // honestly.
  //
  // Only defined for nonbasic variables -- a basic variable's value is a
  // consequence of its row, not a free choice, and the paper is explicit that
  // mutation performs NO pivoting to make one free. Throws std::out_of_range
  // via is_basic()'s accessor for an unknown id; asserts on a basic one.
  struct FreedomInterval {
    std::optional<DeltaRational> lower;
    std::optional<DeltaRational> upper;

    // Whether `value` is inside the interval, treating absent endpoints as
    // infinite. An empty interval (lower > upper) cannot occur on a feasible
    // tableau, but this answers false for it rather than pretending.
    [[nodiscard]] bool contains(const DeltaRational& value) const {
      return (!lower.has_value() || *lower <= value) && (!upper.has_value() || value <= *upper);
    }
  };
  [[nodiscard]] FreedomInterval freedom_interval(LraVarId nonbasic) const;

  // Moves a NONBASIC variable to `value`, propagating the change to every
  // dependent basic variable's assignment and refreshing violation status --
  // i.e. exactly Section 3.1's Update, exposed. QF_LIA patching
  // (theory/arithmetic/lia_patching.{hpp,cpp}, Bjorner & Nachmanson Sect. 4.1)
  // is the only caller: it moves nonbasic values so that Int-sorted variables
  // land on integers.
  //
  // The caller owns feasibility. `value` must lie inside
  // freedom_interval(var), which is precisely the range over which this
  // variable may move with every basic variable staying within its bounds;
  // this function does not re-check that and does not call check().
  //
  // **Not trailed outside PivotingRule::Soi.** pop() restores bounds only (see
  // the class comment on H9), so a move made here cannot be undone by
  // backtracking in the default mode. That is why diversify_shared_model below
  // deliberately runs its whole sweep against a scratch copy of the assignment
  // and stays const. Callers of *this* entry point must instead own a tableau
  // whose assignment need not survive a pop() -- QF_LIA's per-round
  // `lia_tableau` in solve_qf_lia_impl is such a copy, discarded and rebuilt
  // every outer round, which is the same argument lia_gomory_cuts.cpp's module
  // comment already makes for calling add_row() mid-search.
  void set_nonbasic_value(LraVarId var, const DeltaRational& value);

  // The paper's greedy model mutator: walk the nonbasic variables that can
  // move a shared value and, for each, take a value inside its freedom
  // interval if doing so leaves MORE distinct values among `shared`. Fewer
  // coincidences means fewer candidate equalities, and every one avoided is a
  // case split the combination layer does not make.
  //
  // The obligation the paper places on a mutator is monotonicity --
  // |classes(R_M)| <= |classes(R_mutated)| -- which holds here by
  // construction, since an update is kept only when it strictly increases the
  // count. That also gives termination for free: each accepted update raises
  // a quantity bounded by |shared|, so at most |shared| - 1 are ever taken.
  //
  // **Does not touch the tableau.** The sweep runs against a scratch copy of
  // the assignment, so this stays const and poll-only like model_equal_classes
  // and entailed_equalities. That is not fastidiousness: update() trails the
  // old value only under PivotingRule::Soi, and bounds are the only thing
  // pop() restores, so a mutation applied to the live tableau in the default
  // mode could not be undone on backtrack.
  //
  // `classes` holds the non-singleton classes AFTER mutation, in the same
  // shape and order discipline model_equal_classes returns -- it is what the
  // combination layer should branch on. `updates` is the audit trail of what
  // moved, which is what makes a disagreement with the exact oracle
  // debuggable rather than merely detectable.
  struct ModelMutationResult {
    std::vector<std::vector<LraVarId>> classes;
    std::vector<std::pair<LraVarId, DeltaRational>> updates;
    std::vector<DeltaRational> values;
    std::size_t classes_before = 0;
    std::size_t classes_after = 0;
    std::size_t forbidden_equalities_before = 0;
    std::size_t forbidden_equalities_after = 0;

    [[nodiscard]] const DeltaRational& value(LraVarId var) const { return values.at(var); }
  };
  [[nodiscard]] ModelMutationResult diversify_shared_model(
      const std::vector<LraVarId>& shared,
      const std::vector<std::pair<LraVarId, LraVarId>>& forbidden_equalities = {}) const;

  // Read-only access to a variable's current upper/lower bound (if any) and
  // the literal that established it -- used by OnlineLraPropagator's
  // same-slack "deriving satisfied atoms" propagation (Dutertre & de
  // Moura, Section 4.2) to find bounds tight enough to entail other atoms
  // on the same slack variable.
  struct BoundInfo {
    DeltaRational value;
    sat::Lit reason;
  };
  std::optional<BoundInfo> upper_bound(LraVarId var) const {
    const auto& bound = vars_.at(var).upper;
    if (!bound.has_value()) {
      return std::nullopt;
    }
    return BoundInfo{bound->value, bound->reason};
  }
  std::optional<BoundInfo> lower_bound(LraVarId var) const {
    const auto& bound = vars_.at(var).lower;
    if (!bound.has_value()) {
      return std::nullopt;
    }
    return BoundInfo{bound->value, bound->reason};
  }

  // Cumulative count of check() calls (across this tableau's lifetime,
  // including anything replayed via copy) that hit a non-Bland anti-cycling
  // fallback -- always 0 under PivotingRule::Bland. Callers that want a
  // per-round delta (e.g. for a statistics counter) should snapshot this
  // before and after a check() call themselves.
  std::size_t bland_fallback_count() const { return bland_fallback_count_; }

  // PivotingRule::Sparse only: prefer a violated variable with a short row
  // when choosing which one to repair, rather than the most-violated one.
  // This is the *other* Markowitz factor (Monniaux p.8's first sorting
  // criterion), and unlike the entering-side choice it is NOT
  // progress-neutral -- it changes which infeasibility gets repaired -- so it
  // is a separate opt-in knob, measured on its own rather than confounded
  // with the entering-side rule. Off by default; no effect under any other
  // pivoting rule. Intended to be set before solving starts (like add_row);
  // it is not trailed, so flipping it mid-search would change selection
  // without any record of having done so.
  void set_sparse_leaving(bool enabled) { sparse_leaving_ = enabled; }

  // -- Leaving-variable policy (heuristics-note lead 3) --
  //
  // MathSAT's actual rule, from the primary source rather than from a
  // secondary description of it: Griggio's thesis ("An Effective SMT Engine
  // for Formal Verification", Trento 2009) Section 2.5.3, page 53, picks the
  // violated basic variable whose beta is *closest* to its violated bound --
  // the least-violated one -- and breaks ties on the row with fewer non-zero
  // non-basic coefficients. Its stated aim is to minimise the change needed
  // to the tableau and to beta.
  //
  // That is a third point in the design space, and neither of the two Primo
  // already had. The default is most-violated-first (Dantzig-style), and
  // set_sparse_leaving is shortest-row-first with violation as tie-break --
  // each inverts one half of the rule above. See
  // docs/papers/griggio-mathsat-thesis.md, which also records the caveat
  // that the thesis describes MathSAT 4 while King (2014) attributes a
  // most-violated rule to MathSAT5, so this is a candidate to measure rather
  // than a correction to apply.
  //
  // Same gating as set_sparse_leaving: only meaningful under Sparse, and
  // intended to be set before solving starts.
  void set_least_violated_leaving(bool enabled) { least_violated_leaving_ = enabled; }

  // -- Fixed-variable elimination (heuristics-note item H7) --
  //
  // A basic variable whose lower and upper bounds coincide is *fixed*: its
  // row is an equality constraint that must hold exactly, so
  // update_violation_status() marks it violated the moment any non-basic
  // term in it moves, and check() has to spend a repair pivot on it again
  // and again. Real QF_LRA benchmarks are full of these -- an equality
  // `t1 = t2` lowers to two inequalities that share one slack (item H1b),
  // which then carries an upper and a lower bound at the same value -- and
  // on the profiled competition instances they are the dominant source of
  // pivoting.
  //
  // When enabled, assert_upper/assert_lower pivot such a variable out of the
  // basis as soon as it becomes fixed. Once non-basic it is completely inert:
  // can_enter() rejects a fixed variable on every branch (lower == upper ==
  // assignment makes each of its `assignment < upper` / `assignment > lower`
  // tests false), so it is never chosen again and its column never moves.
  // The prior art is LP presolve's fixed-variable removal (Andersen &
  // Andersen, "Presolving in linear programming", Mathematical Programming
  // 71, 1995) -- but note the setting differs in the way that matters:
  // presolve removes fixed variables once, before solving, from a problem
  // whose bounds are all known, whereas here a variable becomes fixed
  // *during* search and must be restored on backtracking. See
  // docs/qf-lra-heuristics.md item H7 for the measurements, and for why that
  // difference is the likely reason it did not pay.
  //
  // The pivot's entering variable is preferred unbounded where possible,
  // since an unbounded basic variable's row can never be violated and so
  // never demands another repair. The pivot is trailed by pivot() and undone
  // by pop() like any other.
  //
  // Off by default, everywhere, including in the solver. The 595-file
  // scrambled competition set is where this was decided: 374 solved against
  // 373 with it off, PAR2 29237 against 29204, and a *median 0.94x* speedup
  // over the files both configurations solve in more than a second -- +19/-18
  // file churn that nets to nothing. The one real effect is confined to the
  // largest instances, where it doubles the >5MB band (7 -> 14 of 50, matching
  // z3), and it is paid for in the 50-500KB band (144 -> 140). Enable with
  // --lra-fixed-elimination when the input is dominated by equalities; see
  // docs/qf-lra-heuristics.md item H7 for the full falsifier outcome.
  //
  // An earlier version also *froze* a qualifying row out of the working set
  // entirely. That half was strictly negative on the same run (it cost four
  // files in the 500KB-5MB band and gained one elsewhere) while carrying all
  // of this item's wrong-answer risk, so it was removed rather than tuned.
  void set_fixed_elimination(bool enabled) { fixed_elimination_ = enabled; }

  // Item H7 observability, same convention as row_update_count() (cumulative
  // over this tableau's lifetime, snapshot for a per-round delta):
  //
  // fixed_variable_count: basic variables observed to become fixed.
  // elimination_pivot_count: pivots spent eliminating them. The shortfall
  //   against fixed_variable_count is the eliminations that did not happen at
  //   all -- an empty row, or every candidate already fixed itself.
  //
  // These all count into pivot_count() as well, since they are real pivots.
  // The published :lra.pivots statistic is a different matter and is NOT
  // comparable across an H7 ablation on its own: check_sat.cpp samples it as
  // a delta around check(), while elimination pivots happen inside
  // assert_upper/assert_lower, so they fall outside that window entirely.
  // :lra.pivots dropping with H7 on therefore does not by itself mean less
  // pivoting -- add :lra.elimination-pivots, or compare
  // :lra.pivot-row-updates / :lra.pivot-term-ops, which count every pivot's
  // actual work regardless of where it was triggered.
  std::size_t fixed_variable_count() const { return fixed_variable_count_; }
  std::size_t elimination_pivot_count() const { return elimination_pivot_count_; }

  // Cost accounting for the Check/Pivot loop, in the two quantities item H3
  // actually minimizes -- pivot *count* alone cannot distinguish "fewer
  // pivots" from "cheaper pivots", and the whole risk of a sparsity rule is
  // that it trades one for the other. Cumulative across this tableau's
  // lifetime; callers wanting a per-round delta snapshot before and after,
  // the same convention pivot_count() uses.
  //
  // row_update_count: rows rewritten, i.e. sum of |occurs_in(entering)|.
  // row_term_op_count: terms merged while rewriting them.
  std::size_t row_update_count() const { return row_update_count_; }
  std::size_t row_term_op_count() const { return row_term_op_count_; }

  // Cumulative count of pivot() calls across this tableau's lifetime --
  // diagnostic only (measuring whether check()'s current eager-per-literal
  // invocation from OnlineLraPropagator::notify_assignment_lit does
  // redundant repair work across closely-spaced assignments, a documented
  // opportunity noted at that call site). Callers that want a per-round
  // delta should snapshot this before/after, same convention as
  // bland_fallback_count().
  std::size_t pivot_count() const { return pivot_count_; }

  // PivotingRule::Soi observability. Always 0 under Bland/Dantzig.
  //
  //   conflict_count  -- times check() built a conflict from the error set
  //                      (Section V-B, Flex(f) empty). Despite the historical
  //                      name this includes SINGLE-row error sets: the only
  //                      guard is `error_vars.empty()`, and 82% of them are
  //                      size 1 in practice.
  //   minimize_calls  -- times minimization ran at all.
  //   minimize_shrunk -- times it actually removed something.
  //   conflict_lits   -- total literals emitted by build_soi_conflict().
  //
  // The last two are separate on purpose. `minimize_shrunk` used to be
  // published as ":lra.soi-quickxplain-calls", which made a 6x under-report
  // of volume look like a low success rate and led to the wrong conclusion
  // about where the waste was. `conflict_lits` is the quantity a minimization
  // strategy is actually trying to reduce -- clause length, not set size.
  std::size_t soi_multi_row_conflict_count() const { return soi_multi_row_conflict_count_; }
  std::size_t soi_minimize_call_count() const { return soi_minimize_call_count_; }
  std::size_t soi_minimize_shrunk_count() const { return soi_minimize_shrunk_count_; }
  std::size_t soi_conflict_lit_count() const { return soi_conflict_lit_count_; }

  // Total number of tableau variables (problem variables plus slacks, plus
  // f_var_ under PivotingRule::Soi). Only grows during initial construction
  // -- see the class comment -- so this is a structural size, not search
  // state. Used to publish the :lra.tableau-vars / :lra.rows /
  // :lra.row-nonzeros statistics, which are what make the tableau's shape
  // (and so the effect of anything that changes how many rows a formula
  // needs) measurable instead of a matter of inference.
  std::size_t variable_count() const { return vars_.size(); }

  // Whether `var` is currently a basic (row-defining) variable -- pivots
  // can change this over time, so this reflects the current pivot state,
  // not a static structural property.
  bool is_basic(LraVarId var) const { return vars_.at(var).is_basic; }

  // `var`'s current row (basic_var = sum(coefficient * var)). `var` must
  // currently be basic (see is_basic()); throws std::out_of_range
  // otherwise, matching this class's existing .at()-style conventions.
  // Note: rows_ stores a slot for every variable (not just currently-basic
  // ones -- see rows_'s own comment), so this only throws for a genuinely
  // invalid `var`, not for a merely-nonbasic one; callers must still gate
  // on is_basic() themselves, since a nonbasic slot's content is stale.
  const Row& row(LraVarId var) const { return rows_.at(var); }

  // The basic variable ids of every row whose terms currently mention
  // `var` -- the reverse of row() -- used by row-based/interval bound
  // propagation to find which rows could have a newly-tightened implied
  // bound after `var`'s own bound changes. Empty (not an error) if `var`
  // occurs in no row. Reflects the current pivot state, kept correct
  // across add_row/pivot/pop (see occurs_in_'s own maintenance).
  const std::vector<LraVarId>& occurs_in(LraVarId var) const { return occurs_in_.at(var); }

  // Whether occurs_in_ agrees with what a full rebuild from rows_/is_basic
  // would produce. pivot() maintains this index by *delta* -- it touches
  // only the variables that genuinely enter or leave a row -- and a
  // divergence would not throw. It would make update() silently skip a
  // dependent row, or sparse pricing misjudge a column, and surface much
  // later as a wrong answer. Order within a listing is not part of the
  // contract and is not compared. Test-only: O(all rows).
  bool occurs_in_index_matches_full_rebuild() const;

 private:
  struct Bound {
    DeltaRational value;
    sat::Lit reason;
  };

  struct VarState {
    bool is_basic = false;
    DeltaRational assignment;
    std::optional<Bound> lower;
    std::optional<Bound> upper;
  };

  struct BoundTrailEntry {
    LraVarId var;
    bool is_upper;
    std::optional<Bound> previous;
  };
  struct AssignmentTrailEntry {
    LraVarId var;
    DeltaRational previous;
  };
  // A pivot's row rewrite touches the pivoted basic/entering pair plus
  // every other basic row that referenced entering_var -- snapshot every
  // row touched (before the rewrite) rather than trying to diff them.
  struct PivotTrailEntry {
    LraVarId basic_var;
    LraVarId entering_var;
    Row old_basic_row;
    std::vector<std::pair<LraVarId, Row>> old_other_rows;
  };
  // PivotingRule::Soi only: records one set_soi_direction() call's effect
  // on f_var_'s row and on `var`'s tracked direction, atomically, so
  // pop() can't desync the two (see set_soi_direction()'s comment).
  struct FRowTrailEntry {
    Row previous_f_row;
    LraVarId var;
    int previous_direction;
  };
  using TrailEntry =
      std::variant<BoundTrailEntry, AssignmentTrailEntry, PivotTrailEntry, FRowTrailEntry>;

  void update(LraVarId nonbasic_var, const DeltaRational& new_value);

  // Shared by the public freedom_interval() and by diversify_shared_model().
  // `beta` overrides the live assignment when non-null, which the mutation
  // sweep needs: after it takes one update, later intervals must be measured
  // against the moved values, not the tableau's.
  [[nodiscard]] FreedomInterval freedom_interval_impl(LraVarId nonbasic,
                                                      const std::vector<DeltaRational>* beta) const;
  void pivot(LraVarId basic_var, LraVarId entering_var);

  // check()'s entering-variable choice, split out so the three rules stay
  // readable side by side. can_enter() is the eligibility test they share --
  // every candidate passing it repairs the violation by the same amount.
  bool can_enter(const util::Rational& coeff, const VarState& state, bool too_low) const;
  LraVarId select_entering_variable(LraVarId violated, bool too_low, bool use_bland);
  LraVarId select_sparse_entering_variable(const Row& row, bool too_low);

  // Item H7 (see set_fixed_elimination): if `var` has just become a fixed
  // basic variable, pivot it out and, when the entering variable's resulting
  // row constrains nothing, deactivate that row. Returns whether an
  // elimination pivot was performed. No-op unless fixed_elimination_ is set.
  bool try_eliminate_fixed_basic(LraVarId var);
  // Entering choice for an elimination pivot. Unlike check()'s, every term of
  // the row is eligible (the point is to leave the basis, not to repair a
  // violation in a particular direction), so this ranks rather than filters:
  // an unbounded candidate first, then the Markowitz occurrence count, then a
  // +/-1 coefficient, then index. Already-fixed candidates are skipped
  // outright -- pivoting one of those *into* the basis is the state this
  // whole mechanism exists to get out of.
  LraVarId select_elimination_entering_variable(const Row& row) const;
  // Whether `var`'s lower and upper bounds coincide, i.e. it is a constant
  // for the remainder of the current search level.
  bool is_fixed(LraVarId var) const;
  // Whether `var` currently has no bound on either side. An unbounded basic
  // variable's row can never be violated, so it never demands a repair pivot
  // -- which is why the elimination pivot prefers such a candidate.
  bool is_unbounded(LraVarId var) const;

  // Whether assignment changes and pivots go on the trail at all. False for
  // the paper's own scheme (bounds only, see the class comment); true only
  // under PivotingRule::Soi, whose explicitly-maintained objective row is
  // basis-dependent and so has to be restored.
  bool trails_assignments_and_pivots() const { return rule_ == PivotingRule::Soi; }

  std::vector<sat::Lit> build_conflict(LraVarId violated, bool too_low) const;
  void undo(const TrailEntry& entry);
  void rebuild_occurs_in_index();

  // Removes `occurs_in_[var][slot]` in O(log |row|), given the slot from the
  // row entry that is going away. Swap-and-pop rather than erase/remove: an
  // erase shifts every later element and would invalidate their back-indices,
  // whereas a swap moves exactly one, whose row entry is then repaired.
  void unlink_occurrence(LraVarId var, std::uint32_t slot);

  // Keeps violated_vars_ (see its own comment) in sync going *forward*:
  // checks `var`'s current is_basic/assignment/bounds and inserts/erases
  // it from the set accordingly. Called from the two places a basic
  // variable's violation status can change during ordinary (non-undo)
  // operation: assert_upper/assert_lower's is_basic branch, and update()'s
  // dependent-basic-variable assignment-change loop. A non-basic variable
  // is never a member (there is nothing to remove if `var` just became
  // non-basic via pivot() -- see pivot()'s own call site for why that's
  // still handled correctly without a dedicated hook there).
  void update_violation_status(LraVarId var);

  // Full O(vars_.size()) rebuild of violated_vars_ from scratch, mirroring
  // rebuild_occurs_in_index()'s existing precedent: pop() can undo bound/
  // assignment/pivot/f-row trail entries in combinations too varied to
  // patch incrementally without real correctness risk, but pop() itself
  // is rare (once per backtrack, not once per pivot), so a full rescan
  // here is cheap relative to what it replaces.
  void rebuild_violated_vars();

  // -- PivotingRule::Soi (Section V) --

  // eq (3)'s directional multiplier: -1 if `var` (assumed currently
  // basic) is below its lower bound, +1 if above its upper bound, 0 if
  // within bounds (or unbounded on the relevant side).
  int soi_direction(LraVarId var) const;

  // The incremental core of Section V's f-row maintenance ("efficiently...
  // by instrumenting UPDATE"): if `new_direction` differs from `var`'s
  // last-recorded direction, merges `(new_direction - old) * var_row`
  // into f_var_'s row (rebuilding that Row + f_var_'s occurs_in_ entries)
  // and records an FRowTrailEntry. `var_row` is passed explicitly (rather
  // than read via rows_.at(var) internally) because the leaving-variable
  // reset call in pivot() must run *before* that row is erased from
  // rows_ -- see pivot()'s call site.
  void set_soi_direction(LraVarId var, int new_direction, const Row& var_row);

  // One SOISELECT call (Fig 5, Section V-A): picks the entering variable,
  // leaving row, and step size that most reduces the sum-of-infeasibilities
  // objective, via the breakpoint-sorted DeltaVio(j,delta) walk. Returns
  // nullopt if no candidate makes progress (Flex(f) is empty) -- the
  // caller must then check whether E is also empty (satisfiable) or not
  // (stuck -- build_soi_conflict()).
  struct SoiSelection {
    LraVarId entering;
    LraVarId leaving;
    DeltaRational delta;
  };
  std::optional<SoiSelection> soi_select() const;

  // f_S = sum of d_i*row_i over only `subset` -- shared by
  // build_soi_conflict and soi_subset_still_conflicts.
  std::unordered_map<LraVarId, util::Rational> restricted_soi_row(
      const std::vector<LraVarId>& subset) const;

  // Section V-B's multi-row conflict, restricted to `error_vars` (the
  // full violated set, or a minimize_soi_conflict()-shrunk subset of it):
  // f_S's own row (same sign-branching pattern as build_conflict) plus
  // every var in `error_vars`'s own bound reason.
  std::vector<sat::Lit> build_soi_conflict(const std::vector<LraVarId>& error_vars) const;

  // Standard QuickXplain (Junker 2001, cited by the paper as [21]) over
  // `error_vars`, using an is-still-a-conflict oracle that computes a
  // *restricted* f_S (sum of d_i*row_i over only a candidate subset) and
  // checks whether any nonbasic variable could still locally reduce it.
  // Returns a subset of `error_vars` sufficient for build_soi_conflict to
  // still produce a sound conflict -- not necessarily globally minimal,
  // but usually is in practice (paper: "most of the time... |S|=2").
  std::vector<LraVarId> minimize_soi_conflict(const std::vector<LraVarId>& error_vars) const;

  // Deletion-based minimization: visit `error_vars` in soi_minimization_order_
  // and drop each element whose removal leaves the remainder still a
  // conflict. Soundness does NOT rest on the predicate being monotone --
  // every removal is validated by soi_subset_still_conflicts() on the
  // resulting set, so whatever comes back independently certifies a
  // conflict. Monotonicity would only bound how small a set is reachable,
  // and soi_subset_still_conflicts is not obviously monotone: adding a row
  // changes f_S and can introduce an eligible nonbasic variable.
  std::vector<LraVarId> delete_soi_conflict(const std::vector<LraVarId>& error_vars) const;

  // `error_vars` reordered worst-first for delete_soi_conflict().
  std::vector<LraVarId> ordered_for_deletion(const std::vector<LraVarId>& error_vars) const;
  bool soi_subset_still_conflicts(const std::vector<LraVarId>& subset) const;
  // The recursive QuickXplain step: minimal subset of `candidates` that,
  // combined with `background`, still conflicts. Standard algorithm
  // (Junker 2001) -- not itself paper-specific, just the well-known
  // divide-and-conquer minimization technique the paper cites by name.
  std::vector<LraVarId> quickxplain(const std::vector<LraVarId>& background,
                                    const std::vector<LraVarId>& candidates) const;

  // PivotingRule::Sparse's bounded scan and check()'s Bland fallback point.
  // Both were hardcoded constants until 2026-08-06; the rationale for the
  // defaults, unchanged, is on Tuning above.
  //
  // Rows reach several hundred terms on real instances (691 on
  // latendresse/ecoliFBAyicesTest-500), and scoring all of them would spend
  // on selection what the rule is trying to save on the pivot -- which is
  // why the scan is bounded at all.
  //
  // The pricing limit caps only how many eligible candidates are *scored*.
  // It must never cut the scan short of establishing that at least one
  // exists: "no eligible entering candidate" is exactly what check() reports
  // as a conflict, so giving up early without having found one would
  // manufacture an unsound conflict rather than merely a worse pivot. See
  // select_entering_variable.
  Tuning tuning_;

  PivotingRule rule_;
  bool sparse_leaving_ = false;
  bool least_violated_leaving_ = false;
  bool fixed_elimination_ = false;
  // Rotating start offset for Sparse's bounded candidate scan. pivot()
  // rebuilds rows in sorted-by-variable-id order, so always starting at the
  // front would bias the bounded scan toward low-numbered variables; rotating
  // spreads it. Search state, not tableau state -- deliberately not trailed,
  // since it only affects which equally-valid pivot is chosen.
  std::size_t sparse_pricing_cursor_ = 0;
  std::size_t bland_fallback_count_ = 0;
  std::size_t pivot_count_ = 0;
  std::size_t row_update_count_ = 0;
  std::size_t row_term_op_count_ = 0;
  std::size_t soi_multi_row_conflict_count_ = 0;
  std::size_t soi_minimize_call_count_ = 0;
  std::size_t soi_minimize_shrunk_count_ = 0;
  mutable std::size_t soi_conflict_lit_count_ = 0;
  std::size_t fixed_variable_count_ = 0;
  std::size_t elimination_pivot_count_ = 0;
  std::optional<LraVarId> f_var_;
  // soi_directions_/rows_/occurs_in_ are all dense, indexed directly by
  // LraVarId (a plain index into vars_, not an opaque key) -- a further
  // profiling follow-up beyond combine_scratch_'s own fix (see its
  // comment): rows_/occurs_in_ were still std::unordered_map, so every
  // pivot's rows_.erase+emplace and occurs_in_ insert/erase was itself a
  // per-call heap-allocated map-node churn, independent of and additional
  // to the combine step's own cost -- re-profiling after the
  // combine_scratch_ fix confirmed this (std::__detail::_Map_base still
  // hot). Since LraVarId is dense and only grows during initial tableau
  // construction (add_variable()/add_row(), always called before the
  // first push()/check() -- see the class comment), sizing these vectors
  // in lockstep with vars_ (in add_variable()) makes every index always
  // valid: a slot for a currently-nonbasic variable simply holds stale,
  // unread content (rebuild_occurs_in_index() gates by is_basic rather
  // than by presence, exactly to make this safe -- see its own comment).
  std::vector<int> soi_directions_;
  std::vector<VarState> vars_;
  std::vector<Row> rows_;
  std::vector<std::vector<LraVarId>> occurs_in_;
  // Currently-violated basic variable ids (assignment outside its own
  // bound), kept incrementally in sync -- see update_violation_status()'s
  // comment. A sorted set gives Bland's smallest-index selection for
  // free and lets Dantzig/SOI's own scans iterate just this (typically
  // small) set instead of every variable in the tableau.
  // `soi_direction(var) != 0` is exactly equivalent to `var ∈
  // violated_vars_` by construction (both mean "basic and currently out
  // of bounds"), so this one set also serves SOI's error-var collection
  // in check() -- no separate SOI-specific bookkeeping needed.
  std::set<LraVarId> violated_vars_;
  std::vector<TrailEntry> trail_;
  // set_soi_direction()'s f-row combine buffer, and since item H8 its only
  // user: pivot()'s affected-row loop used to share it but now merges two
  // ordered rows straight into the rebuilt row, needing no scratch at all.
  // The allocation history below is why it is a reused flat vector rather
  // than a fresh container, and still applies to the remaining user.
  // Profiling (perf, on
  // tmp/QF_LRA/sc/sc-21.induction3.cvc.smt2 and
  // tmp/QF_LRA/uart/uart-18.induction.cvc.smt2, both of which time out at
  // 10s while z3/cvc5 solve in under 1s) showed malloc/free/GMP-allocation
  // machinery consuming 50-60% of runtime. A first attempt reused an
  // unordered_map member instead of a fresh local -- that did NOT help:
  // unordered_map is node-based, so every element is its own heap
  // allocation regardless of whether the container itself is reused
  // (clear() frees each node). A flat vector, sorted then merged, does the
  // same combine with only this member's own backing array (which clear()
  // does NOT deallocate) -- no per-element heap allocation at all beyond
  // each Rational's own unavoidable GMP storage. With H8 leaving a single
  // user there is no sharing question left; LraTableau is not reentrant in
  // any case.
  std::vector<std::pair<LraVarId, util::Rational>> combine_scratch_;
};

}  // namespace primo::theory::arithmetic
