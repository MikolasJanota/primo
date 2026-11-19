# Tactics and preprocessing preservation map

This audit accompanies [tactics](../implementation/tactics.md) and
[preprocessing](../implementation/preprocessing.md), checked against their
implementations on 2026-10-01. The original pages and anchors remain available
in the historical book.

**Preserved** identifies an explanatory destination for a current mechanism.
**Superseded** names a correction or an intentionally different organization.
**Retained history** identifies material preserved in the dated archive.
Its experiments are not rerun by this audit. Archive paths and redirects are
installed and checked at each build; reader acceptance and publication are separate.

## Tactics

| Original section | Disposition and destination |
| --- | --- |
| {source}`Introduction and algorithmic basis <docs/tactics.md#L1-L33>` | Preserved in the [chapter introduction](../implementation/tactics.md) and {ref}`implemented subset <tactics-evidence>`, citing de Moura–Passmore §3 through the shared bibliography. |
| {source}`Implemented data structures <docs/tactics.md#L34-L118>` | Preserved in {ref}`representation <tactics-representation>`, {ref}`goal truth <tactics-goal-truth>`, and the [model chapter](../implementation/models.md). |
| {source}`Immutable strategy and compilation <docs/tactics.md#L119-L182>` | Preserved in {ref}`strategy identity <tactics-strategy-identity>`; the lifetime requirements for stored term IDs and borrowed session objects limit strategy reuse. |
| {source}`Tacticals and probes <docs/tactics.md#L183-L204>` | Preserved in {ref}`composition <tactics-composition>` and {ref}`strategy identity <tactics-strategy-identity>`, including retention of the final iteration’s model records and cooperative deadlines. |
| {source}`Production composition <docs/tactics.md#L205-L321>` | Preserved in {ref}`session-to-engine flow <tactics-production>` and {ref}`preprocessing order <preprocessing-order>`. The initial passes run before profile selection. |
| {source}`Exhaustive Boolean splitting <docs/tactics.md#L322-L372>` | Preserved in {ref}`splitting <tactics-experiments>`: complete alternatives, deterministic selection, model ownership, exponential storage growth, and the limit on selected atoms. |
| {source}`Sequential evaluation <docs/tactics.md#L373-L413>` | Retained history: focused manifests, repeated measurements, branch costs and results. The new chapter makes no claim that splitting improves performance. |
| {source}`Composition verification <docs/tactics.md#L414-L462>` | Current contracts preserved in {ref}`checks <tactics-evidence>`; the runner and SLURM manifest remain linked. Earlier instructions and provenance remain in the dated record; the [current composition workflow](../development/experiments.md#tactic-composition-checks) records procedures and tool limitations. |
| {source}`Deviations, invariants and complexity <docs/tactics.md#L463-L526>` | Preserved across {ref}`composition <tactics-composition>`, {ref}`identity <tactics-strategy-identity>`, {ref}`scope <tactics-incremental>` and {ref}`paper subset <tactics-evidence>`. The blanket statement that incremental tactics are absent is superseded by the restricted set of tactics checked for incremental use. |
| {source}`Incremental execution contract <docs/tactics.md#L527-L542>` | Preserved in {ref}`incremental use <tactics-incremental>` and its links to both assumptions-engine designs. |
| {source}`Tactic-reference catalogue <docs/tactic-reference.md#L1-L157>` | Mechanisms preserved in both chapters; builders, parameters and tactic counters are listed in the [tactic reference](../reference/tactics.md). |
| {source}`Unrepresented transformations and portfolio limitations <docs/tactic-reference.md#L158-L192>` | Preserved in {ref}`session portfolios <tactics-experiments>`, {ref}`strategy identity <tactics-strategy-identity>` and {ref}`limitations <tactics-evidence>`: no tactic parser, learned portfolio, or isolated parallel executor. |

## Preprocessing

| Original section | Disposition and destination |
| --- | --- |
| {source}`Introduction and arithmetic atom canonicalization <docs/preprocessing.md#L1-L228>` | Current scheduling and the two distinct normal forms preserved in {ref}`execution order <preprocessing-order>` and {ref}`arithmetic normalization <preprocessing-extended>`, linked to [LRA](../implementation/lra.md). Dated A/B comparisons and default decisions remain history. |
| {source}`Experimental QF_UF portfolio <docs/preprocessing.md#L229-L442>` | Fallback conditions based on the result and reason code preserved in {ref}`session portfolios <tactics-experiments>`. Screens, family-specific observations and rejected configurations remain history. |
| {source}`Boolean unit propagation <docs/preprocessing.md#L443-L476>` | Preserved in {ref}`round order and facts <preprocessing-round>`, including retained units, sharing and model records. |
| {source}`Forced atom propagation <docs/preprocessing.md#L477-L529>` | Preserved in {ref}`facts <preprocessing-round>` and {ref}`representation <preprocessing-representation>`: congruence, known disequalities, retained facts and later finite-domain use. |
| {source}`Solved constants and Gaussian elimination <docs/preprocessing.md#L530-L656>` | Preserved in {ref}`definition elimination <preprocessing-round>`, {ref}`integer profiles <preprocessing-profiles>`, and {ref}`conversion example <preprocessing-example>`. Historical measurements remain at the old location. |
| {source}`Monotone elimination, preconditions and reconstruction <docs/preprocessing.md#L657-L743>` | Preserved in {ref}`one-sided variables <preprocessing-final>` and the [model conversion chapter](../implementation/models.md#following-a-model-back-through-preprocessing). The local attribution note remains the rule's design reference. |
| {source}`Historical monotone examples and counters <docs/preprocessing.md#L744-L859>` | Current repair rule and counter purpose preserved in {ref}`final passes <preprocessing-final>` and {ref}`evidence <preprocessing-evidence>`. Development examples and measurements remain history; their old helper locations are not current API guidance. |
| {source}`Pure literals and theory atoms <docs/preprocessing.md#L860-L923>` | Preserved with a concrete contradiction in {ref}`propagating facts <preprocessing-round>`. |
| {source}`Real-equality lowering <docs/preprocessing.md#L924-L976>` | Preserved in {ref}`normalization boundary <preprocessing-extended>`. The implementation also handles Int; the new text makes that explicit. The test for equalities introduced by later passes remains part of the composition checks. |
| {source}`ITE lowering <docs/preprocessing.md#L977-L1083>` | Preserved in {ref}`extended terms <preprocessing-extended>` and [integer input translation](../implementation/arithmetic/lia/preparation.md#input-lowering-and-integer-bounds). Growth controls are distinguished from mandatory elimination. |
| {source}`Constant ITE simplification <docs/preprocessing.md#L1084-L1155>` | Current CITE subset, cache scope, identities valid for integers, and ordering preserved in {ref}`extended terms <preprocessing-extended>`. Arena-growth test linked through {ref}`correctness checks <preprocessing-evidence>`. Timing and the evidence for enabling the pass by default stay at the old location. |
| {source}`Constructor versus pass work <docs/preprocessing.md#L1156-L1208>` | Preserved in [core representation](../implementation/core.md) and {ref}`final Boolean normalization <preprocessing-final>`. The current bounded second pass is explained rather than restoring unconditional constructor flattening. |
| {source}`Equality bridge <docs/preprocessing.md#L1209-L1247>` | Preserved with the diamond example and scope in {ref}`equality consequences <preprocessing-round>`. |
| {source}`Finite-domain clauses <docs/preprocessing.md#L1248-L1375>` | Preserved in {ref}`finite domains <preprocessing-finite>`: scans before and after simplification, distinctness, numeral restrictions, existing equality atoms, known disequalities, equality definitions and growth. July probe results remain history. |
| {source}`UF symmetry breaking <docs/preprocessing.md#L1376-L1449>` | Preserved in {ref}`symmetry <preprocessing-finite>`: invariance, fixed terms, value precedence, exclusion of terms affected by the permutations, and model-renaming justification. Historical solver comparisons remain history. |
| {source}`Implementation notes <docs/preprocessing.md#L1450-L1555>` | Current profile, fixed-point and adaptive behavior preserved in {ref}`profiles <preprocessing-profiles>` and {ref}`order <preprocessing-order>`. Historical tuning observations remain at this link. |
| {source}`Mixed-theory opportunities <docs/preprocessing.md#L1556-L1626>` | Retained research record. Current purification is explained in [combination](../implementation/combination.md); proposals are not presented as additional implemented preprocessing passes. |
| {source}`Current status <docs/preprocessing.md#L1627-L1694>` | Superseded as the current narrative by the profile and pass descriptions checked against the code. Historical counts and formerly deferred work remain dated evidence. |
| {source}`Paper traceability <docs/preprocessing.md#L1695-L1719>` | Preserved and qualified in {ref}`sources <preprocessing-sources>`; bibliography identifies papers, while earlier development inspiration stays retrospective. No old solver implementation is an implementation source. |
| {source}`Structural selector design <docs/structural-auto-preprocessing.md#L1-L105>` | Selection semantics preserved in {ref}`profiles <preprocessing-profiles>`; thresholds and analysis costs appear in the [policy reference](../reference/policies.md#structural-preprocessing-selection). |
| {source}`Structural selector evaluation <docs/structural-auto-preprocessing.md#L106-L124>` | Retained history, including the rejected default promotion. The current selector remains legacy. |

## Evidence identities

The original chapters retain the detailed experiment tables. Relevant
{source}`run-log entries <docs/run-log.json>` include:

- `2026-08-30-tactic-refactor-full-slurm-comparison` and
  `2026-08-31-tactic-refactor-infrastructure-profile`: refactoring comparison
  and investigation of tactic infrastructure costs.
- `2026-09-01-tactic-composition-slurm-short-circuit-audit` and
  `2026-09-01-tactic-composition-slurm-merge-gate`: composition coverage,
  including why a later leaf need not execute on an already settled input.
- `2026-08-12-structural-implies-benchmark-f88bc87` and
  `2026-08-17-arithmetic-canonicalization-default-off-979d911`: selector/rewrite
  and canonicalization decisions with their measured scope.
- `2026-09-06-lead16-cite-simplification-133177-133466` and
  `2026-09-07-cite-hoisting-lifetime-slurm-134014`: CITE evaluation and the
  later allocation-boundary repair. The later repair is not silently covered
  by the earlier evaluation.

Historical local runs are provenance, not instructions to run current solver
batches locally. This rewrite ran documentation checks only.

## Reference and workflow destinations

The complete [tactic inventory](../reference/tactics.md) and
[policy reference](../reference/policies.md) preserve builders, counters,
selector thresholds and analysis costs. The
[composition workflow](../development/experiments.md#tactic-composition-checks)
covers supported profiles, revision checks, activity requirements and separate
model validation. The [fuzzing guide](../development/fuzzing.md) also covers
seed staging and integer/finite-domain procedures.

This audit covers the named chapters and selector note. It does not mark every
linked literature note or open lead as migrated, and it does not assign reader
acceptance. Their file-level migration records remain in force.
