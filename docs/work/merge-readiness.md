# Arithmetic branch merge preparation

**Status: documentation consolidation reviewed; merge decision pending.** This page
tracks preparation of `lia-integrality`; it does not record a merge or publication.
The [capability summary](../capabilities.md) describes current behavior,
[research questions](../leads/index.md) identify remaining algorithmic work, and
[evidence](../evidence/index.md) separates completed studies from open questions.

## Integration baseline

On 19 November 2026, HTTPS fetch confirmed `origin/main` at
`fcc274c23ff5b146cc43d80b1b0317faf91c7f08`. It is an ancestor of the reviewed branch
tip `6e4f0cfdd4831f0d3e4068bb172445f285034c26`: zero main-only commits and 249
branch-only commits. No main-side integration is missing at that baseline.
Refresh this check before the actual merge if either tip changes.

This documentation pass changes no solver code, options or executable examples.
It carries forward the source qualification below; it is not a new independent
whole-source audit or a new performance experiment.

## Implementation and qualification evidence

| Area | Current disposition and evidence |
| --- | --- |
| Integer arithmetic | Hybrid branching, uniform cubes and the selected integrality profile are QF_LIA defaults. QF_UFLIA keeps its previous defaults. The [default-promotion record](../evidence/qf-lia-defaults-20261119.md) owns the comparison, correctness gates and regressions. |
| Bound propagation | Row propagation, preprocessing-bound inference and the redundancy filters are implemented. Their controls remain opt-in; [the mechanism study](../evidence/bound-propagation.md) and [combination study](../evidence/arithmetic-combinations.md) distinguish their measured effects. |
| EUF and theory combination | Conflict-selected Ackermann clauses, demand-driven equality registration and the symmetry repair are implemented; the optional mechanisms retain their defaults. The [EUF review](../evidence/euf-performance-fixes.md) and [SMT-COMP follow-up](../evidence/euf-performance-fixes.md#full-smt-comp-selection-follow-up) delimit the evidence. |
| Documentation | Current implementation chapters explain behavior. Open leads name residual questions. Completed, rejected and blocked studies have evidence navigation, including original checklists retained at stable paths. |

The [previous branch review](../evidence/merge-readiness-20261119.md) records the
source review and qualification before the QF_LIA default change. The later
default record qualifies source `b3218ec919336110d79ab2dea9f52a0868ca263a` and
native binary SHA-256
`b7fdf6ad1df4f64a7b84fa1dffd1e18f11edf8868913952c036dc780a2183ae1`.
The two subsequent direct-include repairs at `c1abfeee` produced byte-identical
Release objects. The evidence was committed at the review baseline `6e4f0cfd`.

The later [statistics simplification](../reference/statistics.md#typed-instrumentation-policy)
has its own source and correctness record in the [run ledger](../run-log.json).
It deliberately changes reporting order while retaining counter names and values.
The earlier arithmetic binary hash and timing results do not identify or measure
this later source revision; no new performance comparison is claimed.

The earlier arithmetic qualification includes release and ASan suites, generated and real-seed
mutation gates, separate Dolmen model validation, whole-tree static scans and
SLURM timing comparisons. Exact counts, commands, jobs and artifact identities
belong to the linked records and ledger, rather than a second checklist here.

## Limits retained for review

- The QF_LIA default comparison uses a selected 200-input set. It establishes a
  substantial gain over the prior profile there, with losses; it does not select
  a globally optimal configuration. Cube-only and combined configurations are
  close in aggregate, and several cube-relative losses remain relevant.
- The full UF/UFLIA study compares particular feature configurations. It is not
  a whole-branch performance comparison against `main`.
- Hybrid integer branching implements ordinary splits and a partial
  Griggio-style policy. Proof-derived combination splits and a general
  termination argument remain open.
- The initial preprocessing-bound qualification remains
  [blocked on three quarantined resource-abort inputs](../leads/preprocessing-bound-tightening.md#qualification-is-blocked).
  Later exclusions and unrelated gates do not resolve those historical cases.
- Static-analysis advisories remain documented; passing the prescribed scans
  does not mean their advisory output is empty. Optional mechanisms with mixed
  evidence remain optional.

These limitations are explicit review inputs, not claims that fresh experiments
have closed them. The [research index](../leads/index.md) owns algorithmic leads;
[evidence work](evidence.md) owns unresolved provenance and checking tasks.

## Closing this preparation

Independent read-only review accepts the documentation consolidation, including
current-status accuracy, classification, navigation and retained anchors. Strict
builds of both books, local/Git link checks and lint pass; rendered navigation
and status tables have been inspected. The [review ledger](../review.md) records
the scope, findings and verification. This acceptance covers the documentation
pass, rather than a new whole-branch implementation review.

Actual integration still requires owner review of the candidate and the checks
required on the resulting `main` revision. If solver source changes after the
qualified revision, rerun the applicable source checks and correctness gates.
This page grants no publication or deployment approval; those remain in the
[publication runbook](publication.md).
