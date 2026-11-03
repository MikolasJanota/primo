# Reducing exported theory lemmas

**Status: implemented experimental reduction; cluster qualification pending.**
The target is to remove whole conflict records from a QF_UF export while keeping
its initial CNF fixed. Reducing literals inside a conflict is a different task.

## Mathematical contract

Let B be the captured propositional CNF and let Lᵢ be the learned clause obtained
by complementing conflict record i. For a set S of record indices, define

```text
p(S) = UNSAT(B ∧ ⋀ { Lᵢ : i ∈ S }).
```

This is a propositional query. All input clauses and definitions in B are hard;
only the recorded theory-lemma clauses are selectable. Adding selected clauses
preserves UNSAT, so p is monotone. The original run supplies p(all), which the
separate reduction oracle must independently establish before pruning.

`core` returns some S satisfying p(S). `mus` returns such an S for which removing
any one remaining record makes the propositional problem SAT. This is subset
minimality relative to B, not minimum cardinality and not minimization of B.
If B alone is propositionally UNSAT, the correct result is an empty record set.

## Paper basis and implementation checklist

The basis is Marques-Silva, Janota and Belov, *Minimal Sets over Monotone
Predicates in Boolean Formulae*, CAV 2013, pp. 592–607,
[author manuscript](https://sat.inesc-id.pt/~mikolas/cav13.pdf),
[DOI](https://doi.org/10.1007/978-3-642-39799-8_39).
Use Section 4.1 for the monotone predicate, Section 4.3 for initial core
refinement, and Section 5.1, Algorithms 3–4, for progression and transition
search. Proposition 2 is the minimality argument to preserve.

The implementation maps these operations as follows:

| Owner | State and operation |
| --- | --- |
| {lemmareductionsource}`QfUfConflictCollector <src/smt/qf_uf_export.hpp>` in the SMT layer | Retain raw initial clauses and raw theory clauses only when reduction is requested; keep their original record indices aligned with printed records. |
| {lemmareductionsource}`sat::select_unsat_subset <src/sat/clause_subset.cpp>` | Own a fresh incremental-safe CaDiCaL backend. Reserve original variables and allocate a new selector sᵢ for each Lᵢ. Add B permanently and add `(¬sᵢ ∨ Lᵢ)` permanently. |
| {lemmareductionsource}`sat::select_unsat_subset <src/sat/clause_subset.cpp>` | Query a set S by assuming exactly its positive selectors. Unselected selectors occur only in their guards and can be false. Failed positive assumptions after UNSAT identify a valid initial core. |
| {lemmareductionsource}`util::minimal_set_progression <src/util/minimal_set.cpp>` | Keep necessary indices M and an unprocessed suffix W. Probe removal blocks of size 1, 2, 4, …; on a SAT boundary, binary-search the transition element, keep it in M, and restart at block size 1. |
| SMT export adapter | Apply the returned indices to conflict records, preserving order and literal contents. Do not export selectors or alter the initial CNF or signature. |

The progression invariant is p(M ∪ W), with every member of M necessary relative
to M ∪ W. A successful removal keeps that invariant; a transition search adds
an element whose removal makes the predicate false. Monotonicity preserves the
necessity of earlier elements as W shrinks. When W is empty, M is an MUS relative
to B. Binary search must maintain a known UNSAT lower boundary and a known SAT
upper boundary. Unknown is neither boundary and must abort the claimed result.

Expected work is one initial core query followed, for `mus`, by the paper's
progression queries. Constructing each assumption set and copying input clauses
also costs time; this design makes no wall-time claim. Count actual SAT calls,
core removals, block probes, binary probes, and maximum block width so tests can
show whether the progression mechanism runs.

Implemented scope is the generic Algorithms 3–4 plus an initial
failed-assumption core. The specialized Algorithms 5–6, repeated core refinement,
model rotation, model-guided pruning and journal-specific oracle constructions
are deliberately outside this first implementation. Cancellation or unknown
must not yield an artifact advertised as a completed MUS.

{lemmareductionsource}`clause-subset unit tests <tests/unit/clause_subset_tests.cpp>` and {lemmareductionsource}`export CLI and fuzz tests <tests/cnf_export_cli.py>` cover redundant and duplicate records, a purely propositional
contradiction, all records necessary, empty clauses, independent irreducibility
checks on small instances, interruption, and a monotone predicate with a long
irrelevant prefix that requires block skipping and transition search. The last
case requires fewer than 40 queries for 1024 candidates with one necessary
element near the end, and requires both block skipping and binary search.

## Representation, lifetime and failure modes

`QfUfConflictCollector` (`src/smt/qf_uf_export.hpp`) borrows its adapter's term
manager and text spool. With reduction enabled, it owns numeric copies of the
input and theory clauses until serialization. Its `theory_clauses_` indices
correspond one-to-one to the conflict spool's byte-range records; clauses are
neither interned nor deduplicated. Reduction returns indices without mutating
or reading the text. Publication copies those ranges in original order.
With `none`, binding releases raw input clauses after printing and collection
retains no raw theory clauses or conflict-offset index. No expanded literal
strings are kept. No terms are created by reduction. Main solving and normal
preprocessing finish before the separate SAT oracle is run.

{lemmareductionsource}`select_unsat_subset <src/sat/clause_subset.hpp>` borrows two immutable `sat::Cnf` vectors for one call and
owns the fresh `CadicalBackend`, guard clauses and selector vector. Its backend
uses the incremental-safe plain profile, independently of main-solver options.
The optional `sat::Terminator` is borrowed for that call. SAT state and selectors
are destroyed before returning {lemmareductionsource}`ClauseSubsetResult <src/sat/clause_subset.hpp>`, which owns only indices,
status and counters. Zero literals, `INT_MIN`, invalid modes and selector index
overflow are rejected. Failed assumptions are queried only after UNSAT and
before the next solve; each solve receives fresh assumptions.

{lemmareductionsource}`minimal_set_progression <src/util/minimal_set.hpp>` borrows the original ordered index vector and callback.
An offset represents W, while an owned vector holds M. Trial vectors copy M and
the remaining suffix; the input is never mutated. Its returned indices preserve
original order. An unknown predicate result returns `complete=false` and the
original known-true index set, rather than claiming minimality.

The export adapter applies a reduction only on completed UNSAT. SAT from the
initial replay signals inconsistent capture and yields an error. Interrupted
reduction returns unknown and leaves the output path untouched. Internal
assumption reuse does not add incremental SMT-LIB support. There is no separate
postprocessor time budget: the caller's cancellation hook applies, while the
SMT theory-conflict budget governs only the original solving phase.

For example, let B contain `(x ∨ y)` and let the candidate clauses be `¬x`,
`¬y`, another `¬x`, and an unrelated `z`. The two negative units suffice;
`core` may retain either copy of `¬x` and may retain redundant clauses, while
`mus` retains exactly one `¬x` and one `¬y`. Dropping either retained clause
makes B plus the remainder SAT. The generic utility test bypasses the initial
core so its progression fast path is exercised even if CaDiCaL's core is small.

The CLI exposes `--export-cnf-reduction=none|core|mus`; `none` is the default.
All modes retain the format's original signature, input clauses, and literal
sequences of retained records. Only whole records are removed. See
[the user reference](../reference/qf-uf-conflicts.md#reducing-theory-lemmas) for
commands and counter names. Runtime and fuzz evidence must identify its exact
source revision; pending qualification is not a correctness claim.

## Other routes examined

These remain alternatives, not implemented features:

- CaDiCaL 3.0's public C++ `Tracer` interface provides original/derived clause
  IDs, antecedents and a final conflict. A backend-owned dependency computation
  can obtain a clausal core without selectors. It needs clause-origin mapping
  and correct treatment of proof transformations. Our adapter does not expose
  this interface and our dependency installer does not install `tracer.hpp`.
- A separate proof-producing replay followed by DRAT/LRAT trimming can also
  recover a core without selectors, at the cost of proof files and another tool.
- Fresh SAT instances per subset query avoid selectors but lose search reuse.
- The native temporary-constraint API handles one temporary clause, not an
  arbitrary selectable set of lemmas.
- The bundled internal Kitten header has explicit clausal-core functions, but
  those belong to a separate embedded solver, not the public CaDiCaL Solver or
  IPASIR-UP interface. Using it would require a separate backend decision.

The proof-interface basis is Biere et al., *CaDiCaL 2.0*, CAV 2024,
Section 5 ([paper](https://cca.informatik.uni-freiburg.de/papers/BiereFallerFazekasFleuryFroleyksPollitt-CAV24-Springer.pdf)).
Fazekas, Pollitt, Fleury and Biere, *Certifying Incremental SAT Solving*, LPAR 2024,
Sections 3–4 ([paper](https://kfazekas.github.io/papers/FazekasPollittFleuryBiere-LPAR24.pdf)),
describes assumption cores and incremental proof dependencies; an assumption core
is not automatically a core of original theory-lemma clauses.

The journal results on query complexity by Janota and Marques-Silva
([Artificial Intelligence 233, 2016](https://doi.org/10.1016/j.artint.2016.01.002))
have special preconditions, including a constant number of minimal sets for the
MUS result. No such condition is established here, so they do not justify a
logarithmic-query claim for arbitrary exported instances. The broader reductions
of Marques-Silva, Janota and Mencía
([Artificial Intelligence 252, 2017](https://doi.org/10.1016/j.artint.2017.07.005))
are related context rather than an additional algorithm implemented here.
