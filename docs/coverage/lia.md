# Integer-arithmetic preservation map

This section-level audit for the [LIA chapter](../implementation/lia.md) was
checked on 2026-09-29 against the original
{source}`QF_LIA development chapter <docs/qf-lia-branch-and-bound.md>`.
The old file and HTML anchors remain available in the historical book.

**Preserved** means that the new destination explains the mechanism or contract.
**Superseded** identifies a claim corrected against the current implementation.
**Retained history** identifies the exact old section preserved in the dated
archive. Retaining history does not revalidate an experiment. The assembled
site now checks the archive paths and redirects at every build.

## Section destinations

Line links identify old sections at the centrally pinned source revision;
destination links lead to explanations in the current manual.

| Original section | Disposition and exact destination |
| --- | --- |
| {source}`Introduction and paper basis <docs/qf-lia-branch-and-bound.md#L1-L56>` | Preserved in {ref}`stage order <lia-schedule>` and {ref}`sources <lia-sources>`. Stage numbers remain development chronology, not execution order. |
| {source}`Scope <docs/qf-lia-branch-and-bound.md#L58-L65>` | Preserved in {ref}`input lowering <lia-input>` and {ref}`limits <lia-limits>`: constant-divisor extensions are supported; QF_LIRA is not. |
| {source}`Where branching lives <docs/qf-lia-branch-and-bound.md#L67-L112>` | Preserved in {ref}`ownership <lia-representation>`, {ref}`scheduling <lia-schedule>`, {ref}`explanations <lia-explanations>` and {ref}`models <lia-models-evidence>`. Historical `check_sat.cpp` locations are replaced by the owning functions. |
| {source}`The search <docs/qf-lia-branch-and-bound.md#L114-L136>` | Preserved in {ref}`branching <lia-branching>`, including return states and checkpoints. Bounds-only rollback is qualified for the optional SOI path. |
| {source}`Int in the frontend <docs/qf-lia-branch-and-bound.md#L138-L190>` | Preserved in {ref}`input lowering <lia-input>` and [core representation](../implementation/core.md). Pass-by-pass policy is preserved in [integer preprocessing profiles](../implementation/preprocessing.md#profiles-and-repetition). |
| {source}`Integer strictness and three bugs <docs/qf-lia-branch-and-bound.md#L192-L306>` | Current rules and polarity examples preserved in {ref}`integer bounds <lia-input>`. Rejected atom-level tightening, naive threshold offsets, wrong auxiliary-variable rounding and their runs remain history at this link. The early delta-free-assignment claim is superseded. |
| {source}`GCD-consistency <docs/qf-lia-branch-and-bound.md#L309-L417>` | Preserved in {ref}`row divisibility <lia-gcd>`, with scaling, both bound reasons and every-variable guards. Bugs 4/5 and the suspected extended-test formula erratum remain history. The early statement that all row terms are declared integers is superseded by the pivot-aware rule. |
| {source}`Gomory cuts <docs/qf-lia-branch-and-bound.md#L420-L531>` | Preserved in {ref}`cuts <lia-cuts>`: eligibility, four coefficient cases, derivation, both worked examples, the `3x+5y=7` regression, private-row lifetime and the rejected need for backtrackable cut rows. |
| {source}`Patching <docs/qf-lia-branch-and-bound.md#L534-L574>` | Preserved in {ref}`patching <lia-patching>`: basic and nonbasic cases, freedom intervals and feasibility. A fixed variable's interval is a singleton, correcting the old “empty” wording. Historical motivation and cube comparison remain at the old location. |
| {source}`Two things the obvious implementation gets wrong <docs/qf-lia-branch-and-bound.md#L576-L611>` | Preserved in [the patching example](../implementation/lia.md#why-a-nearest-move-is-insufficient), including oscillation avoidance, target window, indexed scoring and counters. |
| {source}`The one LraTableau change <docs/qf-lia-branch-and-bound.md#L613-L631>` | Preserved in {ref}`ownership <lia-representation>`, {ref}`patching <lia-patching>`, {ref}`integer helpers <lia-input>` and {ref}`rollback <lia-branching>`. Development chronology remains history. |
| {source}`Patching: Result <docs/qf-lia-branch-and-bound.md#L633-L668>` | Retained history: sample comparison, model checks, unavailable local Dolmen and limits of its substitute. Current obligations are in [the correctness guide](../development/correctness.md). |
| {source}`Patching: Full corpus <docs/qf-lia-branch-and-bound.md#L670-L705>` | Retained history: SLURM 128125, both arms, gains, losses and search-trajectory cost. Current controls/counters are in {ref}`patching <lia-patching>` and {ref}`budgets <lia-limits>`. |
| {source}`Conflict generalization: mechanism <docs/qf-lia-branch-and-bound.md#L707-L770>` | Preserved and corrected in {ref}`explanations <lia-explanations>`. Clause containment establishes neither validity nor sufficiency of the collected reasons. |
| {source}`Conflict generalization: measurements <docs/qf-lia-branch-and-bound.md#L771-L812>` | Retained history: sample comparisons, SLURM 127949, whole-corpus churn and the `rings` regression. The new chapter preserves the tradeoff without presenting the old diagnosis as a fresh measurement. |
| {source}`Node limits <docs/qf-lia-branch-and-bound.md#L814-L831>` | Preserved in {ref}`budgets <lia-limits>`, with actual counter semantics and zero-budget behavior. |
| {source}`Known incompleteness <docs/qf-lia-branch-and-bound.md#L834-L867>` | Preserved in {ref}`the unbounded interval example <lia-limits>`. The suggestion that a cube test could close an integer-infeasible case is superseded: failure of that feasibility test cannot prove infeasibility. |
| {source}`QF_LIRA overview <docs/qf-lia-branch-and-bound.md#L870-L885>` | Preserved in {ref}`representation <lia-representation>` and {ref}`limits <lia-limits>`. `3*x <= 5` is corrected to a direct bound, not an auxiliary row. |
| {source}`Each stage with a non-integer variable <docs/qf-lia-branch-and-bound.md#L887-L914>` | Preserved in the individual {ref}`patch <lia-patching>`, {ref}`GCD <lia-gcd>`, {ref}`cut <lia-cuts>` and {ref}`branch <lia-branching>` contracts. |
| {source}`What blocks QF_LIRA <docs/qf-lia-branch-and-bound.md#L916-L932>` | Preserved in {ref}`limits <lia-limits>`: subset APIs do not provide typing, coercions, admission or mixed model integration. The historical whitelist is not copied as a current global capability list. |
| {source}`Status of the QF_LIRA claim <docs/qf-lia-branch-and-bound.md#L934-L945>` | Preserved in {ref}`limits <lia-limits>`. No end-to-end mixed-input qualification is claimed. The proposed experiment remains a proposal at the old location. |
| {source}`Explicitly out of scope <docs/qf-lia-branch-and-bound.md#L947-L992>` | Current omissions preserved in {ref}`limits <lia-limits>` and {ref}`cuts <lia-cuts>`. Blanket `div`/`mod` rejection is superseded by {ref}`lowering <lia-input>`. The original cube omission is superseded by {ref}`optional QF_LIA cubes <lia-cubes>`. The old cube counts cited from Bromberger remain that thesis's results; fresh Primo measurements are {ref}`separate <lia-cube-evidence>`. |
| {source}`Fuzzing: commands, campaigns and setup <docs/qf-lia-branch-and-bound.md#L994-L1103>` | Retained history: smoke recipes, SLURM 129508/129527, partial mutation coverage, non-passing strict replay, corpus staging and tool cost. Obsolete operator exclusions are not copied into the input contract. Maintained workflow extraction is {ref}`explicit below <lia-remaining-extraction>`. |
| {source}`Fuzzing: defects and stage gates <docs/qf-lia-branch-and-bound.md#L1105-L1222>` | Retained history: nonbasic auxiliary-variable bug, Dolmen grammar handling, failing jobs and replacement gates. Lasting correctness conditions are in {ref}`GCD <lia-gcd>` and {ref}`coverage <lia-models-evidence>`. |
| {source}`Where QF_LIA stands, 2026-08-26 <docs/qf-lia-branch-and-bound.md#L1224-L1265>` | Retained history: revision `16d0d96`, SLURM 130136, comparison table and cactus figure. {ref}`Evidence <lia-models-evidence>` preserves the exclusion of clean `unknown` and timeout rows from solved counts. |
| {source}`Real-world evaluation and the fuzzing gap <docs/qf-lia-branch-and-bound.md#L1268-L1352>` | Failure mechanism and structural seed diversity preserved in {ref}`coverage <lia-models-evidence>`. Failed probes, coefficient-width negative result, repair sequence and counts remain history at this link. |
| {source}`Tests <docs/qf-lia-branch-and-bound.md#L1354-L1403>` | Preserved in {ref}`regression obligations <lia-models-evidence>`, with current test paths and extension support. Old stage numbers and blanket division rejection are superseded. |

## Evidence identities

The original chapter is still the archive location. Relevant entries in the
{source}`run log <docs/run-log.json>` include:

- `2026-08-20-qf-lia-real-world-benchmark-evaluation` and
  `2026-08-20-qf-lia-mutation-fuzz-gate-slurm-verification`: failures missed by
  generated-input campaigns and the pivoted-auxiliary-variable repair.
- `2026-08-21-qf-lia-conflict-generalization` and
  `2026-08-21-qf-lia-conflict-generalization-full-corpus`: clause-size comparisons,
  gains and regressions.
- `2026-08-21-qf-lia-patching` and `2026-08-21-qf-lia-patching-verification`:
  development and qualification records.
- `2026-09-03-qf-uflia-indexed-patching-local`: the later affected-column scoring
  check. Local execution is historical provenance, not a current instruction
  to run solver batches locally.

(lia-remaining-extraction)=
## Destinations outside the integer algorithm chapter

The [preprocessing chapter](../implementation/preprocessing.md) now covers
integer profile selection, restricted optional passes, mandatory translation,
and model conversions from the old frontend policy.

Preserved in [fuzzing](../development/fuzzing.md): seed selection/staging,
mutation restrictions, visited-seed limits, and outcome classification.
[Independent validation](../development/validation.md) records Dolmen grammar
modes. Historical machine-specific counts remain in their dated records.

This audit covers the original LIA development chapter. It does not certify
the separate integer-division literature note, profiling report, all integer
open leads or the whole corpus. Their sections are accounted for in the
[complete inventory](index.md), with current literature and research mappings.
The archive routes are installed; reader acceptance remains pending.
