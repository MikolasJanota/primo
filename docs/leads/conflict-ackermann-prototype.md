# Conflict-selected Ackermann prototype

**Status: checklist written before implementation; prototype in qualification. Both options remain off by default.**

This bounded experiment follows the conflict-selected congruence clause in
Dutertre and de Moura, *The Yices SMT Solver* (2006), §2, printed p. 3
{cite:p}`dutertre2006yices`. The primary PDF was reread for this experiment;
SHA256 `f85699440a641ce4247e57ec5aecf5fd99d126ae1a39692b91a47c8399818fde`.
The paper selects a congruence step used to derive a conflict, exposes its
clause to SAT, and retains the complete UF checker. Our extension to n-ary
functions is the ordinary congruence clause

```text
¬(a1=b1) ∨ … ∨ ¬(an=bn) ∨ (f(a1,…,an)=f(b1,…,bn)).
```

The paper motivates this selection rule, not the budgets below. This prototype
will use only existing SAT equality atoms. Missing atoms, Boolean-result
encodings without a result-equality atom, general fresh-atom registration and
a new clause-retention/deletion policy remain outside it. It is a subset of
the lead, not a complete implementation of Yices dynamic Ackermannization.

## Representation and operations to implement

1. `FlatCongruenceClosure` optionally records at most 64 congruence-edge pairs
   visited by the **actual conflict explanation**. Propagation explanations
   do not feed this selector. Count every visited congruence step, even after
   the prefix budget is full. Keep the ordinary conflict reason unchanged.
2. Store tracing and its checkpoint snapshots only when enabled. Restore the
   owning trace on `pop`; never keep borrowed edge indices past rollback.
   Support one-sided BFS, bidirectional BFS and the old proof-forest mode.
   Reject reoriented/structural proof-forest modes explicitly for this first
   prototype; their optimized explanations currently return only direct leaves.
3. `FlatTermIndex` maps a selected flat result back to an original term only
   if flattening that original term gives that exact result. Curry-prefix
   nodes are not semantic SMT terms, even though definitions carry an original
   application for bookkeeping. Count and skip prefix witnesses.
4. `IncrementalFlatAssignmentState` copies semantic application pairs out of
   the closure before callbacks can backtrack. No theory term is constructed
   or registered during callbacks. `OnlineEufPropagator` owns the selection
   catalogue and copies SAT clauses into its own pending queue.
5. Build an equality-pair index once from the registered atoms, extending it
   at existing generated-equality registration. For each selected pair, require
   the same function symbol and arity. Omit reflexive argument pairs, retain
   the n-ary disjunction, deduplicate literals and discard tautologies.
6. Retain at most 4,096 semantic pairs per propagator and consider clauses
   with arity at most 31: the conservative arity+1 width check happens
   before removing reflexive argument positions or deduplicating literals.
   Thus some high-arity pairs whose simplified clause would fit are skipped.
   Record budget exits. A
   missing pair may be retried after atom registration changes; a ready pair
   is not repeatedly reconstructed. These are engineering bounds.
7. Deliver the ordinary EUF conflict first. Queue auxiliary clauses until a
   subsequent external-clause callback; keep them across SAT backtracking,
   because congruence axioms have no branch-local premises. Stream copied
   integer SAT literals through the adapter. This uses no all-pairs catalogue
   or assignment-wide repeated scan.
8. Auxiliary clauses may be declared forgettable through the existing adapter;
   complete EUF checking remains authoritative. Deduplication tracks selection
   and delivery, not whether the backend still stores or reuses a clause.
   Do not claim measured clause reuse from delivery counts alone.
9. Use separate diagnostic and emitting options, both off by default. Require
   the incremental online/conflict EUF path; explicitly reject unsupported
   configurations. Match the existing experimental modes' assumptions-session
   restriction. Rebuild sessions must preserve query/scope semantics.

The state belongs to one propagator instance. QF_UFLIA atom growth can replace
that instance, resetting its catalogue budgets; already delivered SAT clauses
remain backend-owned. `TermId` and SAT variable
identities are stable for this lifetime; vectors and term records are not
borrowed across registration. The prototype never creates terms or atoms and
never changes the ordinary conflict/propagation explanation.

## Invariants and proof obligations

- Every emitted clause is a full congruence instance, independent of the
  equalities/assertions that selected its pair. Omitting a transitive premise
  from an ordinary explanation must not turn an assertion-dependent fact into
  an unconditional lemma.
- For arity two, a false result equality implies an argument **disjunction**,
  not both argument disequalities. Identical argument positions and repeated
  argument pairs must simplify without strengthening the clause.
- A curry-prefix conflict cannot be mistaken for equality of its enclosing
  complete applications. Valid but irrelevant enclosing clauses also bias the
  selector, so prefix witnesses are counted separately.
- A failed or truncated diagnostic does not suppress the complete EUF conflict.
  Queued clauses remain valid after a backjump or an incremental scope change;
  no callback retains flat IDs after the owning closure rolls back.
- Disabled tracing must not allocate a per-checkpoint trace vector. Enabled
  storage includes O(A) registered equality entries, up to 4,096 catalogue
  entries and pending clauses of at most 32 literals each, plus O(D × 64)
  flat-pair trace storage at checkpoint depth D. Enabled checkpoints copy the
  retained prefix; disabled tracing creates no trace snapshots.

Index construction costs O(A), with A the registered equality atoms. Expected
index lookups are constant time; literal deduplication/tautology checks cost
O(w²) per selected pair for width w ≤ 32; no O(m²) application-pair enumeration is introduced. Trace capture
adds constant work at explanation edges already traversed. These are Primo
engineering expectations, not complexity claims from the Yices paper.

## Verification and decision checklist

Before declaring the prototype implemented and evaluated:

- Test unary/n-ary/nested conflicts, irrelevant congruence merges, Boolean
  terms, shared/reflexive arguments, missing result/argument atoms, trace and
  catalogue limits, rollback, repeated checks, scope retirement, and explicit
  unsupported mode admission. Exercise all supported explanation modes.
- Count conflict trace steps/retained prefixes/truncations, semantic pairs,
  prefix skips, duplicates, missing atoms, eligible/queued/delivered clauses,
  width/catalogue exits and storage peaks. Measure disabled/diagnostic/emitting
  profiles separately. Missing-atom ratios concern the bounded prefix only.
- Freeze the current native baseline and a reproducible QF_UF/QF_UFLIA pilot.
  Use SLURM builds/checks, ASan, lint and source review; run fresh exact-option
  generated and real-seed correctness gates before broad timing. Stop on wrong
  answers, invalid models, crashes or unexplained disagreements.
- Time baseline, candidate-off, diagnostic and emitting profiles with matching
  limits; validate every successful native SAT timing row separately with
  Dolmen. Report solved, PAR2, paired runtimes, memory and selection coverage.
- Keep off by default. If coverage or benefit is weak, record the limitation
  or rejection and identify whether atom growth, selection or overhead is the
  next issue. This bounded pilot is not a global-tuning/default decision.

## Sources and implementation status

The existing owners/hooks are
{conflictacksource}`FlatCongruenceClosure <src/theory/equality/flat_congruence_closure.hpp>`,
{conflictacksource}`FlatTermIndex <src/theory/equality/flat_terms.hpp>`, and
{conflictacksource}`IncrementalFlatAssignmentState / OnlineEufPropagator <src/smt/solver_engines.cpp>`.
The latter file also owns generated-equality registration and callback scheduling.
The {conflictacksource}`external-clause adapter <src/sat/cadical_backend.cpp>` retains backend
watching/learning responsibilities; {conflictacksource}`assumptions admission <src/smt/smt_session.cpp>`
keeps this prototype out of persistent assumptions sessions. The new
{conflictacksource}`ConflictAckermannSelector <src/smt/conflict_ackermann.hpp>` owns only semantic
pair keys, existing equality literals and visit-generation metadata. These
additions are implemented in the working prototype; native qualification and
performance evidence are still pending. The first GCC build/native checks and
ASan passed; whole-tree Clang analysis found a trace-state initialization
compatibility issue. Explicit aggregate initialization replaces `optional::emplace`;
qualification must use the repaired build.

For binary f, the clause is `(a≠b) ∨ (c≠d) ∨ (f(a,c)=f(b,d))`. With false
result equality and `a=b`, only `c≠d` is forced. Trace steps involving internal `f(a)`/`f(b)` curry prefixes are always skipped:
those prefixes are not typed SMT applications of binary f. A separate trace
step naming both complete applications may still be considered.

The QF_UF path resolves `auto` before checking admission. Actual incremental
EUF execution must be `online` or `conflict`; model/off or lazy configurations
are rejected when this path is used. QF_UFLIA requires its online incremental
components. QF_UFLRA is explicitly outside this prototype. Supported explanation
values are `one-sided`, `bidirectional`, and `proof-forest`; optimized forest
values are rejected. Diagnostic-only means the diagnostic flag is on and the
emitting flag is off; the emitting flag also records diagnostics.
