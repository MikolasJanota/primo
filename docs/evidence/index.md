# Evidence and investigation records

Use the [capability overview](../capabilities.md) and implementation chapters for
current behavior. This index records what was implemented, measured, rejected
or left blocked. An experiment's profile and revision remain part of its result;
a completed study does not make every tested option a default.
The [open research index](../leads/index.md) contains the questions that remain.

## Integer arithmetic

Uniform cubes and ordinary-coordinate hybrid branching are implemented. QF_LIA
uses the qualified M124/uniform/hybrid profile; QF_UFLIA retains private branching.

| Record | Outcome and scope |
| --- | --- |
| [Current QF_LIA profile](qf-lia-defaults-20261119.md) | Qualified default change, measured gains and retained private-search regressions. One targeted combined-profile comparison, not a full-corpus combined-profile win. |
| [Cubes](lia-cubes.md) | Completed development and full M124 comparisons; documented sufficient-model subset. |
| [Ordinary hybrid branching](lia-hybrid-20261111.md) | Completed original opt-in comparison and repeated regressions. Later route defaults are recorded separately. |
| [Parameter pilot](lia-tuning-pilot.md) | Completed bounded tuning; no universally best setting or global tuning claim. |
| [QF_LIA progress](qf-lia-progress.md) | Historical full-corpus configurations and their comparisons. |
| [Arithmetic efficiency](arithmetic-efficiency-20261108.md) and [overhead](arithmetic-overhead-20261109.md) | Completed bottleneck investigations; proposed improvements remain in the [efficiency lead](../leads/arithmetic-efficiency-followups.md). |

Original [cube](../leads/lia-cube-test.md) and
[hybrid](../leads/lia-hybrid-branching.md) checklists remain at their existing
paths for stable links and paper/proof provenance. They are completed records;
the implementation chapter owns current representation and scheduling.

## Bounds and propagation

| Record | Outcome and scope |
| --- | --- |
| [Preprocessing with row propagation](bound-propagation.md) | Completed comparison and profiling; aggregate gains were too small to justify promotion. |
| [Behavior inspection](bound-behavior.md) | Diagnoses notification/rounding gaps and duplicate work rather than treating timing noise as evidence of useful propagation. |
| [Bound repairs](bound-repairs.md) | Implemented, qualified repairs and scan-local reuse; mixed performance, row modes remain opt-in. |
| [Redundancy filtering](propagation-redundancy.md) | Implemented pending-literal deduplication and unchanged-input scan memoization within opt-in row mode. |
| [Arithmetic combinations](arithmetic-combinations.md) | Completed technique combinations and complementarity analysis; no universal best full stack. |
| [Read-only detector record](../leads/root-bound-detection.md) | Implemented diagnostic and measured opportunities; it installs no bounds. |
| [Preprocessing bound checklist](../leads/preprocessing-bound-tightening.md) | Implemented experimental pass, original qualification blocked, later placement trial rejected. These remain explicit limits. |
| [Repair/filter checklists](../leads/bound-propagation-improvements.md) | Completed obligations and original protocols, including rejected placement and accepted optional filters. |

These studies retain their measured defaults. They do not override later
QF_LIA profile changes or establish qualification of every optional combination.

## EUF and theory combination

| Record | Outcome and scope |
| --- | --- |
| [Conflict-selected Ackermann pilot](conflict-ackermann.md) | Correctness-qualified bounded prototype; pilot did not show a performance benefit. |
| [EUF fixes and full comparison](euf-performance-fixes.md) | Mechanical fixes and optional demand naming implemented; repeated demand regressions. Full UF/UFLIA comparison and SAT-model stage completed. |
| [QF_UFLIA equality sharing](qf-uflia-equality-sharing.md) | Completed 44-input development and 659-input follow-up comparisons; broad export remains off. |
| [Equality-delivery ablation](qf-uflia-equality-ablation.md) | Implemented/evaluated delivery controls; fixed-only policy retained experimentally. |
| [Model-unit producer](uflia-model-unit-20261111.md) | Implemented, qualified optional integer integration; legacy remains the producer default. |
| [Gap profiling](uflia-gaps-20261110.md), [SMT-COMP selection](uflia-smtcomp2026-20261111.md), [Certora diagnostics](certora-paper-20261111.md) | Specific benchmark/profiling scopes, not a whole-branch-versus-main performance claim. |
| [Model repair](model-repair-20261110.md) | Completed correctness repair with original failure and validation provenance. |
| [QF_UFLIA progress](qf-uflia-progress.md) | Historical reference/configuration comparisons. |

The original [Ackermann V1](../leads/conflict-ackermann-prototype.md),
[V2/mechanical](../leads/euf-performance-fixes.md),
[symmetry repair](../leads/uf-symmetry-composition.md) and
[equality-delivery](../implementation/uflia-equality-ablation.md) checklists
preserve design and proof obligations. Remaining care, equation and clause-lifetime
questions are in [open research](../leads/index.md#equality-and-theory-combination).
The [adaptive phase diagnostic](../leads/adaptive-phase-probe.md) and
[phase-region simulations](../leads/phase-region-simulation.md) are completed
negative investigations; their policies were rejected and no production policy
is retained by those experiments.

## Broader evaluations and maintenance history

The [ground comparison gallery](ground-comparison.md) identifies each plotted
profile and reference provenance. The [October refresh](evaluation-refresh.md),
[long-limit study](long-limits.md) and [uniform 120-second evaluation](uniform120.md)
retain different revisions, corpora and limits. Published QF_LRA results belong
in [the paper report](../papers/primo-qflra-results.md), not the current-default table.

[Publication qualification](publication.md), [technical follow-ups](publication-followups.md),
[static analysis](static-analysis.md), [assignment migration](assignment.md),
[Real arithmetic](arithmetic.md), [LRA regressions](lra-regressions.md) and
[quantifier checking](quantifiers.md) preserve their own acceptance boundaries.
The [pre-promotion branch review](merge-readiness-20261119.md) is a dated review;
[the current merge checklist](../work/merge-readiness.md) owns preparation now.
[Review history](../review-history.md) and the [original-book catalogue](archive.md)
retain older work without presenting it as current capability.

## Historical lead dispositions

The table below preserves closed entries from the previous register. Its
purpose is traceability; [the current research index](../leads/index.md) owns
remaining questions, including their [original numbering](../leads/index.md#original-numbered-questions).
No completed row below should create another implementation task.

### Implemented or closed investigations

| Original entry | Disposition |
| --- | --- |
| 2b: dispatch regression | Repaired at the recorded historical revision; retain the diagnosis in the evidence archive. |
| 3: least-violated leaving rule | Implemented as an option; the original comparison did not justify promotion. See [LRA](../implementation/lra.md). |
| 3b and 7: occurrence/row layout | The current LRA chapter describes the implemented sorted rows and occurrence maintenance. Hash-set and earlier indexed-removal prototypes remain rejected history. The column-to-row direction remains open as [column-to-row coefficient access](../leads/lra-column-coefficients.md). |
| 9: Real monotone elimination | Implemented, optional. Later full-corpus measurements differ from the early small-sample rejection; see [arithmetic evidence](../evidence/arithmetic.md). |
| 10: model-unit equality producer | Implemented as an optional producer. Legacy remains default; this is not an unimplemented algorithm. |
| 12a/12f | Integer-safe preprocessing and patching have current explanations. The old claim that all integer preprocessing is disabled is superseded by the route-specific policies. |
| 12d: integer conflict generalization | Implemented and enabled by default. {ref}`Integer explanations <lia-explanations>` collect the reasons used by the search; {source}`generalized_lia_lemma <src/smt/solver_engines.cpp#L8334-L8360>` checks the result and retains the coarse blocking clause as a fallback. |
| 15: bridge atoms | Removed. Do not restore it as an available alternative or open migration task. |
| 20: model-phase evaluation | Evaluated on arithmetic divisions. Model-phase hints default on for declared QF_LRA rebuild queries with automatic preprocessing, and off on other routes. See {ref}`automatic LRA heuristics <automatic-lra-heuristics>` for the policy and its qualification. |
| Shared assignment ownership and rollback markers | Implemented/repaired; theory undo logs remain separate. See [DPLL(T)](../implementation/dpllt.md). |
