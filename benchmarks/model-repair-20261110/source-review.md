# Model-reconstruction cache repair: source review

Scope: the complete tracked non-generated project inventory, with the implementation
review focused on the changes from `2fd342bd` to the candidate. The
whole-tree lint, C++ static scans, and regression results are recorded in
`qualification.json` as each stage finishes; this document does not substitute for
those results or its explicit acceptance status.

## Summary

The repair changes the validity of delayed finite-domain inputs, rather than
weakening model validation. Profile analysis happens before solve-eqs and forced
value propagation. Cached generated clauses must not reintroduce constants
eliminated by those transformations. Both the raw AMO result and optional symmetry
result now require the exact same ordered root IDs in the same live term manager.
A mismatch regenerates constraints from the executing goal. Model guards,
conversion ordering, solver options, limits and search algorithms are unchanged.

## Blocking issues

None identified in the implementation review. The recorded test, differential,
ASan and independent printed-model gates have finished. Resource timeouts are
reported separately; the mutation sample retains one incomplete resource row.

## Soundness and lifetime review

- `FiniteDomainPreprocessInput` owns its root vector and an immutable shared raw
  result. The optional symmetry result is owned by value. The context must use
  the same live manager; numeric IDs alone cannot enforce that precondition.
- The tactic captures that input by value. Each nonterminal alternative compares
  its own roots; a refresh never overwrites another alternative's snapshot.
- Regeneration calls the existing generators with current roots. The optional
  post-AMO scan still receives current assertions and reconstruction facts.
- `refreshed_raw` owns its vectors for the entire emission operation. `raw` points
  either to that local owner or the captured immutable result. Neither owner is
  mutated while its clause vector is used. Term creation grows the term manager,
  not those result vectors; clauses and assertions carry stable IDs.
- No new borrowed term nodes, child spans or references survive a term-creating
  call. The test DAG visitor is read-only and mutates only its separate worklist.
- The final component-model assertion guard, external validation and reverse
  conversion replay remain unchanged. No theory explanation, SAT callback,
  arithmetic operation or backtracking protocol is modified.

## Hygiene and API review

The input field name now covers both AMO and symmetry snapshots. The owning type
and comments state the cache boundary. There are no new implementation-only
public types or dependencies. C++ follows repository clang-format; strict warnings
caught a missing optional initializer in the new fixture, which was corrected.
Whole-tree static results are reported separately, including advisory warnings;
a green exit status is not described as a warning-free codebase.

The completed scan covers all 135 tracked C++ translation units. Its 407
include-cleaner diagnostics match the accepted baseline, with no new missing or
unused include. The three additional advisories flag string construction in the
new eight-case model fixture; this bounded setup is outside solver runtime and
is intentionally retained. Production diagnostics, including the pre-existing
Boolean-flattening const-move advisory, match baseline. See `static-review.json`
for occurrence counts, normalization rules, source locations and dispositions.

## Tests

The ten-line regression fails on the frozen original binary. New model tests
cover Int/Real UF, both forced function values, and post-AMO enabled/disabled.
Tactic tests check unchanged hits, changed roots, independent alternatives,
absence of eliminated symbols, and symmetry refresh. Existing tests retain their
SAT/UNSAT assertions and now expect counts from regenerated constraints. Initial
profile-analysis counters retain their old meaning; emitted clauses may decrease
when simplification removes the original opportunity. A six-fixture SLURM probe
records those changes independently of test assertions.

## Efficiency and remaining limits

This is a correctness repair, not a speed optimization. Root comparison is linear
in the assertion-vector length; mismatch can add a finite-domain or symmetry
scan. The comparison runs once per nonterminal alternative at this delayed stage,
not in SAT callbacks. Exact-root matching is deliberately conservative: even a
harmless reorder refreshes. Reusing old clauses would need a separate proof and
reconstruction transport contract. This patch makes no corpus speedup claim or
claim to close the QF_UFLIA/cvc5 gap. The unchanged-root fixture demonstrates that
reuse remains available. No performance/default promotion is inferred from the
corrected original seed's model-production time.
