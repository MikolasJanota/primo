# Hybrid branching parameter pilot

This is a bounded development study of existing integer-branching controls,
with correctness qualification before timings. It follows the
[arithmetic combination study](arithmetic-combinations.md). It is not global
tuning, a representative ranking, a default promotion, or an implemented
profile selector. Preprocessing is off throughout. The row-on context enables
both forward and reverse row propagation; same-variable propagation remains
enabled in both contexts.

The {download}`protocol <../../benchmarks/lia-tuning-20261116/protocol.json>`
and {download}`selection <../../benchmarks/lia-tuning-20261116/selection.json>`
are frozen before new runs. Forty inputs, twenty per integer logic, form a
target-enriched diagnostic pilot. Required cases include prior hybrid gains,
the two lost LIA refutations, and UF/row interactions; remaining slots use a
deterministic family round-robin. Three known long cases with no recorded
private-node or split activity are excluded from this timing pilot only.
The tracked correctness regression corpus remains intact. Selection from
previous outcomes makes this unsuitable for claims of global optimality.

| Policy | Private factor | Private node cap |
| --- | ---: | ---: |
| PVT | private mode | 100,000 |
| H0 | 0 | 100,000 |
| H1k / H1 | 1 | 1,024 / 100,000 |
| H16k / H16 | 16 | 1,024 / 100,000 |
| H4096k / H4096 | 4,096 | 1,024 / 100,000 |

The factor-zero duplicate at the smaller cap is omitted because its private
budget is zero under either cap. The two lost LIA refutations previously used
19,806 and 70,539 private nodes. The large factor therefore brackets a regime
that a factor-one neighbourhood alone would miss. It does not reproduce
private mode exactly: hybrid uses its history-based selection and can still
hand off after the cap.

The existing budget is `min(node cap, factor × original Int term count)` per
completion attempt. Factor zero does not bypass earlier enabled patch/GCD/cube/cut stages. Eight policies × two row contexts × forty inputs × three repetitions
produce **1,920 primary native measurements**, at **30 seconds**, with an
8 GiB address-space cap. Orders rotate and reverse by case/repetition; this
is a pilot, not a statistical significance test. A later full-budget and
independent global evaluation is required before selecting defaults.

## Measured results

The larger private factor has a real effect on the targeted LIA bottleneck.
Both factor-4,096 policies solve 18/20 LIA cases in every repetition, versus
17/20 for factor one and 15/20 for private mode, in both row contexts. Relative
to factor one, they recover the two cut-lemma refutations and lose one slack
instance. This is a changed solved set, not a broad uniform runtime speedup.

The cap is also active: 1,024 reduces private work on one pseudo-Boolean LIA
instance and saves about three seconds. It does not change the solved set in
this pilot. UF evidence is weaker at this short deadline: factor zero with
rows off solves one additional case; all row-on policies solve only 3/20.
There is no setting that wins on every targeted input. No defaults change.

Counts below are identical in all three repetitions. PAR2 cells give the mean
of the three per-repetition means, in seconds per input. Each unsolved row costs
60 seconds. The full audit retains each repetition, its runtime distribution,
paired conditional runtimes, gains/losses, family summaries and memory/counters.
These selected 40 inputs do not support a global ranking or significance claim.
Three repetitions do not fully balance eight profile positions.

### QF_LIA

| Policy | Rows off solved /20 | Mean PAR2 (s) | Rows on solved /20 | Mean PAR2 (s) |
| --- | ---: | ---: | ---: | ---: |
| PVT | 15 | 17.07 | 15 | 16.36 |
| H0 | 15 | 16.48 | 15 | 15.76 |
| H1k | 17 | 11.76 | 17 | 11.06 |
| H1 | 17 | 11.92 | 17 | 11.21 |
| H16k | 17 | 12.88 | 17 | 12.18 |
| H16 | 17 | 13.04 | 17 | 12.33 |
| H4096k | 18 | 8.75 | 18 | 8.05 |
| H4096 | 18 | 8.90 | 18 | 8.20 |

### QF_UFLIA

| Policy | Rows off solved /20 | Mean PAR2 (s) | Rows on solved /20 | Mean PAR2 (s) |
| --- | ---: | ---: | ---: | ---: |
| PVT | 4 | 51.87 | 3 | 51.93 |
| H0 | 5 | 50.12 | 3 | 51.93 |
| H1k | 4 | 51.85 | 3 | 51.93 |
| H1 | 4 | 51.88 | 3 | 51.93 |
| H16k | 4 | 51.87 | 3 | 51.95 |
| H16 | 4 | 51.87 | 3 | 51.93 |
| H4096k | 4 | 51.85 | 3 | 51.92 |
| H4096 | 4 | 51.86 | 3 | 51.92 |

### What changed in search

These rows-off times are medians of the three primary repetitions; **TO**
means the 30-second deadline expired. Cleanup makes stored timeout wall times
about 31 seconds; the score remains 60 seconds. Parent slots identify inputs
in the published selection and measurement CSV.

| Input (parent slot) | PVT (s) | H0 (s) | H1 (s) | H16 (s) | H4096k (s) |
| --- | ---: | ---: | ---: | ---: | ---: |
| `cut_lemma_01_005` (85) | 9.638 | TO | TO | TO | 0.233 |
| `cut_lemma_01_010` (95) | 2.646 | TO | TO | TO | 0.310 |
| `slacks/15-7` (40) | TO | 0.086 | 0.564 | 22.962 | TO |
| `normalized-j301_4-unsat` (61) | TO | TO | 27.644 | 27.523 | 24.450 |

The recovered refutations use exactly **614 and 729 private nodes** with both
factor-4,096 policies, with no external split request. Private mode uses 70,539
and 19,806 nodes. Comparing with private mode also changes variable selection:
hybrid uses the history policy, so this comparison does not isolate budget
size from selection. Factors one and 16 time out on these two inputs.

On `slacks/15-7`, H0 uses zero private nodes and three SAT resumes; H1 uses 98
nodes and three resumes; H16 uses 1,075 nodes and two resumes. Their median
private-completion timers are 0.074, 0.551 and 22.950 seconds respectively.
This exposes private arithmetic work directly, rather than inferring it from
small PAR2 differences. The factor-4,096 runs time out without final counters;
no unfinished-search counters are imputed.

On `normalized-j301_4-unsat`, compare **H1k with H1** to isolate the cap: medians
are 24.470 versus 27.644 seconds, with 1,781 versus 2,613 private nodes. The
lower cap causes one external split/SAT resume; the higher cap completes
privately. The same counter change occurs at factors 16 and 4,096. The cap
applies per completion attempt, so total private nodes can exceed 1,024.

The extra UF completion is parent slot 210, Certora `38347..._58_QF_UFLIA`:
H0 with rows off returns SAT in a median 25.144 seconds with one generated atom
and zero private nodes; H1 and private mode time out. Rows on loses this gain.
Most selected UF inputs remain censored at 30 seconds, and its common-case
runtime differences are small. This pilot cannot rank UF settings generally.

Peak observed native RSS is 890.2 MiB; per-profile/context medians and maxima
are in the full audit. Successful-row counter totals exclude incomplete search;
normal resource-return snapshots remain separate. Printed cancellation snapshots
on timeout rows remain in the CSV. Phase timers overlap and
must not be added as disjoint costs. All 663 SAT timing rows passed separate
Dolmen checks; correctness qualification and finite-cap probes are described
below.

The decision is to retain these results as development evidence. Both early
SAT handoff and a larger private-search regime belong in the later global
tuning effort, which needs independent inputs and longer deadlines. This study
uses the existing default engine/equality settings; it does not evaluate an
alternate online/offline or equality-producer matrix, nor choose a new profile.

## Correctness and execution

The immutable binary is source `4f02a8b7`, SHA256
`08948789d787e2a81e76ad671738f29ac192dde34b4c9276789dff55b03a44ff`.
It was built through the preparation helper with `--build-via-slurm` and
passed 49 native groups and 357 Python tests. Production C++ is unchanged;
this study adds only local metadata/configuration/analysis tools. The
existing frozen qualification and timing/model runners execute on SLURM;
only private configuration/input metadata is staged. The preparation helper
checks this binary immediately before each new submission. The
{download}`environment inventory <../../benchmarks/lia-tuning-20261116/environment.json>`
records all 240 timing allocations across eight x86-64 nodes with matching recorded topology
(56 logical CPUs, two sockets, 14 cores/socket, two threads/core; configured
SLURM `RealMemory=257525`). Each allocation reports affinity to two logical CPUs;
physical-core isolation was not established. It also pins the Z3 and Dolmen
executables by SHA256. The retained versions are Z3 5.1.0 and Dolmen 0.10;
their hashes were freshly rechecked under the qualification PATH.

Generated comparisons use 5-second native/reference limits. Tracked sessions
use 120 seconds; original-seed and mutant rechecks use 10 seconds. The mutation
driver has a 600-second campaign budget, 30 iterations/seed and 5-second solver
limit. Gate SAT replays use 30 seconds native/30 seconds Dolmen, except tracked
sessions (120/30). Gate allocations request `--mem=8G`. Primary/probe timing allocations
request `--mem=10G`; the timing runner sets an 8 GiB native address-space cap.
Their model-replay allocations request `--mem=10G` and use 180/60 seconds.
The address-space cap applies to the timing phase. The separate private-node diagnostic replay uses 120 seconds.
These correctness budgets are excluded from primary speed scores.

Every one of the 32 logic/context/policy tasks receives 1,000 fresh generated
cases against Z3, the exact-option tracked regression sessions, and real-seed
mutations. Original regression sessions must complete and agree before the
mutation stage starts. All native SAT gate results, including mutation
originals and reference timeouts, require direct Dolmen acceptance.
Timeouts and diagnosed resource unknowns remain separate from agreement
passes. Wrong answers, crashes, invalid models or unexplained disagreements
stop the study.

Separate resource probes use published successful multi-split cases and atom
caps 0, 1, 8 and 64. Zero means unlimited fresh generated atoms per solve,
not zero split requests. These are correctness probes, not tuning scores. They
check concrete answers when completion succeeds, exact resource diagnostics
when capped, and no acceptance of an answer followed by nonzero exit. All
successful probe SAT results are independently checked. No logical UNSAT may
be inferred from atom-budget exhaustion. Their original, uncapped input and
profile identities are retained for comparison.

Only audited gates permit primary timings. Three timing arrays run in
sequence, followed by model arrays for **every** successful SAT timing row.
Model replays add `--check-model`, use 180 seconds native/60 seconds Dolmen,
and never enter speed scores. PAR2 charges each unsolved primary row 60
seconds. Report per-logic/context and first-two-path-component family summaries,
solved counts, paired runtimes, gains and losses against H1/PVT, memory, private nodes/conflicts/budget exits, split
requests/atoms/resumes and overlapping phase timers. Incomplete statistics
are not zero-filled. Ranking is descriptive for this selected pilot only.

## Status and sources

The local {liatuningtools}`configuration generator <scripts/qualification/lia_tuning/prepare.py>`
freezes recipes and selection; the {liatuningtools}`96-receipt auditor <scripts/qualification/lia_tuning/audit.py>`
checks exact options, input/configuration hashes and all-SAT coverage. The
{liatuningtools}`submission controller <scripts/qualification/lia_tuning/submit.py>`
preflights and holds arrays until their whole-array dependencies are verified.
The finite-cap configuration/audit tool is
{liatuningtools}`finite-cap and resource-replay auditor <scripts/qualification/lia_tuning/probes.py>`. All 32 native probes and
10 SAT model replays passed. The
{download}`qualification inventory <../../benchmarks/lia-tuning-20261116/qualification.json>`
accepts all 96 exact-option receipts: 32,000 generated inputs (31,810 agreement
passes, 188 timeouts and two reference timeouts), 256 tracked regression
measurements, 384 original real seeds (all agreement passes), and 11,520
mutants (11,230 agreement passes; 289 native timeouts and one diagnosed
private-node-limit unknown). All **29,767 successful native SAT gate results**
passed Dolmen. Resource-incomplete cases do not become agreement passes.
The {download}`unknown replay <../../benchmarks/lia-tuning-20261116/resource-unknowns.json>`
confirms the exact private-node diagnostic and positive budget-exit counter;
the {download}`finite-cap inventory <../../benchmarks/lia-tuning-20261116/resource-probes.json>`
contains 10 SAT, eight UNSAT and 14 diagnosed atom-limit unknowns.
All three primary timing arrays and all three model arrays completed. The
{download}`full audit <../../benchmarks/lia-tuning-20261116/full/analysis.json>`
accepts 1,920 rows, all 663 successful SAT timing rows independently checked by
Dolmen, with 939 timeouts and six diagnosed private-node-limit unknowns.
The {download}`measurement CSV <../../benchmarks/lia-tuning-20261116/full/measurements.csv>`
retains every repetition, parent input slot, row context, outcome, RSS and
available counter. Family summaries use the first two path components.
Paired comparisons include changes to one factor or cap, in addition to
H1/PVT controls. Counter totals are conditional on successful completion. Printed timeout
snapshots are retained in the CSV and excluded from successful-row totals;
unprinted timeout counters remain unavailable. Defaults remain unchanged.

The native budget and history belong to
{arithmeticcombsource}`lia_branch_and_bound.cpp <src/theory/arithmetic/lia_branch_and_bound.cpp>`;
{arithmeticcombsource}`CLI registration <src/main.cpp>` defines the controls.
The frozen {arithmeticcombsource}`qualification runner <scripts/qualification/arithmetic_combinations/qualify.py>`,
{arithmeticcombsource}`cohort router <scripts/qualification/arithmetic_combinations/run.py>`
and {arithmeticcombsource}`native timing/model runner <scripts/qualification/lia_hybrid/run.py>`
execute the private JSON metadata. The older fixed-72-receipt auditor is not
used for this pilot.

The existing policy implements the ordinary-coordinate subset of Griggio §5,
with SAT scheduling motivated by splitting on demand; see the
[implementation checklist](../leads/lia-hybrid-branching.md). Proof-derived
combination splits and a new termination claim are outside this study.
Parameter choices here are engineering experiments based on prior diagnostic
counts, not paper-prescribed optimum values. Configuration/analysis metadata
has no ownership of C++ terms, tableau rows, SAT literals or solving state;
it references content-pinned inputs and the existing driver protocol.
