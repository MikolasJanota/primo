# Source and efficiency review

Baseline: `6c8d1278`. Reviewed production snapshot: `183dce9a`; qualified
source/test snapshot: `44eee3c1`. The latter changes only a regression assertion
and documentation relative to the production snapshot. The review uses the
repository `smt-code-review` and external `solver-efficiency-review` checklists.

## Summary

No blocking issue found. Keep discovery selectable and default off with the
existing export switch; do not promote model-unit or fixed/all delivery from
this diagnostic sample. The paper source is Bjørner–Nachmanson, CAV2024 §3.2,
read from the supplied PDF before the local implementation checklist.

## Soundness and ownership

The query's exact current-value classes restrict candidates, not proof truth.
Unit tableau identities and bounds fixing the remaining variables establish
each equality in the rational relaxation and therefore in every integer model.
An owned copy is repaired at the complete online boundary because bounds-only
rollback does not promise a feasible assignment. A failed repair returns its
ordinary explained conflict. Completion receives the repaired snapshot by move
when discovery returns no batch. No synthetic integer cut/branch reasons can
enter this pre-completion query. Existing active-reason validation rejects zero
or inactive literals and translates blocking reasons exactly once.

`LiaComponentAdapter` copies the enum and owns the query result. No query result
borrows the tableau. Endpoint TermIds are scalar snapshots; this added path
creates no terms. Existing driver registration remains the only bridge to atom
creation, with owning explanations and no term or SAT-model borrow used after
growth. The table query uses local vectors/maps and path compression without
retaining a borrow across mutation of its backing container. There is no new
SAT/theory or SMT-LIB layer shortcut.

Model coincidence alone emits no proof. Incomplete unit discovery does not
replace complete arrangements, integer completion or final model validation.
Pure LIA has no shared interface. Selecting the producer with export disabled
returns before discovery and before the new snapshot path. Delivery policy
still controls filtering/naming/proof clauses independently.

## Tests and hygiene

Focused tests cover disabled activation, both producers and delivery settings,
pre-completion equality proofs, online/rebuild and model-guided/complete routes,
non-unit compatibility, model coincidence and pure LIA isolation. Existing
paper-specific tableau probes cover both unit signs, non-unit rejection,
large fixed coefficients, deterministic spanning and rollback. Unit and ASan
gates, 8,000 fresh differential trials (5,320 content-distinct inputs), original seeds and mutations, and direct
Dolmen replay provide the recorded evidence; they are not a proof of all inputs.

The whole tracked non-generated inventory was included in mechanical hygiene
checks: C++ formatting, Python lint/syntax, document links, and all 135 C++
translation units under clang-tidy/analyzer/include-cleaner. Manual review
focuses on the changed adapter, selector plumbing, owning query, callers,
reason translation, tests and workflow scripts. The static review records
pre-existing warnings and the new enum/test advisories rather than calling the
whole tree warning-free. No new production helper or include is unused. The
public API adds one enum and one option; implementation state stays private.
No dead alternative implementation or copied solver source was introduced.

## Efficiency findings and decision

The unit query skips legacy exact-offset discovery, but exact model grouping,
full eligible-row traversal, reason allocation and spanning-forest selection
remain. The adapter still constructs shared-variable/reason lookup maps at
each scan. This integration deliberately reuses the measured existing query;
no unsupported incremental grouping claim is made. Copy+feasibility repair is
measured separately and applies only to enabled complete online discovery.

The 12-case quick diagnostic shows lower discovery time on examples with equal
recorded search, but delivery may select a different equality forest and alter
SAT search substantially. Model-unit/all gains one solve over legacy/all;
model-unit/fixed loses one. Additional paired runs investigate the observed
regressions. These mixed results reject default promotion, while allowing the
requested correctness-qualified research option. Broader corpus/M124/default
promotion evidence remains absent. The next experiment is controlled proof
forest/bridge delivery on the MathSAT Wisa regression, with new scheduling
contracts before changing code.

## Deferred work

Incremental exact model grouping, fixed-only specialized discovery, explanation
selection and delayed bridges need separate designs and gates. They are not
required to expose the requested producer safely. Existing enum layout and
options-padding advisories remain consistent with neighboring options; no
unrelated layout migration is folded into this change.
