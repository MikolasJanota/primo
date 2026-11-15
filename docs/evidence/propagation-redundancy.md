# Redundant arithmetic propagation work

This experiment distinguishes repeated work from newly derived constraints.
The instrument-only audit found substantial duplication in the pending
SAT-literal queue and repeated scans after bounds that did not strengthen the
tableau. The implementation filters those two operations under the existing
opt-in row-propagation option. It adds no arithmetic rows, variables, atoms or
inference rules. Option defaults are unchanged.

The final candidate is frozen at `c815a0b4`; its native SHA256 is
`3a40b72a4837017a1b3d39dc70e0454b9c39796975d9e66f56271bd61c41e9ed`.
The working branch `lia-integrality` implements commit `b62e9649` with identical
production source; its
unit test adds only the direct `sat/lit.hpp` include after the frozen scan,
with the test body unchanged and a successful bounded syntax-only compile.
Merge `7e16e7d6` preserves all measured snapshots in this branch's ancestry without
changing its tree. Source correctness, static qualification and the full paired comparison are
complete. The filters reduce overhead on reliably solved slower cases, most
clearly in QF_UFLIA. Overall solved counts change only at a timeout boundary;
this is not a broad improvement in solving coverage. The implementation remains
confined to the existing opt-in row mode.

## What the audit found

On the two selected QF_LIA cases at a normal stop after 1,000 theory conflicts,
about 89% of queue insertions repeated a signed literal already pending. Over
99% of row-scan events added no new pending literal, and 29–31% of bound
notifications changed neither numerical endpoint. These fractions describe
specific completed prefixes, not the whole corpus or CPU time. In particular,
an event that adds nothing can still be necessary for correctness.

The audit preserves reasons, duplicates and order. Its 120 complete/prefix
measurement rows have 14 successful SAT rows, all separately checked by
Dolmen. Two Callgrind attempts did not complete within 300 seconds and yielded
no usable dump. Four normally completed GDB sampling prefixes recorded 468
wall-stack observations; they support investigating row-scanning work, but
are not CPU percentages or native timing evidence.

The dedup-only and combined prototypes retain the first pending literal and
its explanation. At the same 1,000-conflict QF_LIA stops, dedup-only paired
candidate/control runtime ratios were approximately 0.90–0.96; adding the
repeated-state guard gave approximately 0.78–0.81. Recorded core search counters
matched. These are single diagnostic prefixes against the instrument-only
control. They establish reduced work at those stops, not a complete-solve
speedup. In the combined prototype's selected 120-second complete runs, the
four persistent timeout cases still timed out, while the Wisa SAT case improved.
The approximately 39-second private-integer-search case was essentially
unchanged. All four successful SAT rows in that complete probe passed Dolmen.

The prototype `127d71e1` was superseded before acceptance: its disabled queue
still allocated markers by SAT index. The final candidate bypasses marker
storage outside row mode and uses Boolean presence in coalesced mode.
Earlier prototype receipts qualify only the identified prototype.

Supporting receipts:

- {download}`audit prefixes <../../benchmarks/propagation-redundancy-20261115/prefix-observations.json>`
  and {download}`120-row audit comparison <../../benchmarks/propagation-redundancy-20261115/audit-comparison.json>`;
- {download}`stack samples and failed Callgrind attempts <../../benchmarks/propagation-redundancy-20261115/stack-samples.json>`;
- {download}`dedup-only prefixes <../../benchmarks/propagation-redundancy-20261115/dedup-prefix.json>`
  and {download}`combined prefixes <../../benchmarks/propagation-redundancy-20261115/filter-prefix.json>`;
- {download}`24-row complete hard probe <../../benchmarks/propagation-redundancy-20261115/hard-comparison.json>`.


## Implementation and limits

[The implementation description](../implementation/lra.md) and
{ref}`design checklist <repeated-state-row-scan-filter>` explain the owning
`ArithmeticPropagationQueue`, its first-occurrence reason invariant, and
`ArithmeticRowScanMemo`. The memo skips a no-op row scan only for the same
arithmetic variable, trail offset, cumulative pivot count and backtrack
generation. Backtracking invalidates stamps; component registration recreates
the execution. Same-variable propagation, equality/interface processing and
feasibility checks remain active. Scan-local caches and their borrowed references
end with the scan; queued and delivered explanations own vectors that survive it.

This is conservative scheduling and ordinary signed queue coalescing motivated
by Schulte–Stuckey, *Efficient Constraint Propagation Engines*, §5. It is not an
implementation of their finite-domain fixpoint algorithm. Row-bound arithmetic
continues to use the implemented subset of Dutertre–de Moura's interval
propagation. The [paper-grounded checklist](../leads/bound-propagation-improvements.md)
records the papers, ownership, proof obligations, costs and unsupported scope.

Both adapters serve QF_LRA/QF_UFLRA and the rational relaxation of
QF_LIA/QF_UFLIA. The evaluation uses forward and reverse row propagation,
with and without preprocessing bound tightening, called `rows` and `both`.
It does not qualify a default-profile performance claim. Existing arithmetic
rounding policies, scan caps, private integer search and fallback settings stay
fixed. The dense arrays in enabled mode still scale with the largest encountered
SAT/arithmetic IDs, including gaps.

## Final-source correctness qualification

| Gate | Scope | Result |
| --- | --- | --- |
| `make check` | Native and Python suites in SLURM | 49 native groups and 354 Python tests pass |
| `make asan-test` | Fresh sanitized build and native suite in SLURM | 49 groups pass |
| Generated differential gate | 3,000 inputs per affected logic, 12,000 total | 11,973 agreements; 27 timeouts remain incomplete |
| Generated SAT models | Every selected native SAT case, direct Dolmen | 9,084 pass |
| Real-seed mutations | 2,520 mutants from 12 seeds per logic/profile | 2,512 agreements; eight reference timeouts remain incomplete |
| Mutation SAT models | Every selected native SAT case, direct Dolmen | 1,704 pass, including the eight reference-timeout cases |
| Tracked regressions | 57 cases in each profile | Both profiles pass; each has 27 single-query and six incremental SAT queries checked by Dolmen |
| Static/include scans | All 140 tracked translation units, both inventories | Both complete with tool exit zero; unused-header gates pass; 865 tidy and 398 include-provider findings retained |

No wrong answer, crash or invalid selected model was observed in these gates.
The mutation child with reference timeouts deliberately exits nonzero; its
incomplete status is retained. Resource-incomplete cases are not counted as
reference agreements. The {download}`qualification receipt <../../benchmarks/propagation-redundancy-20261115/final-qualification.json>`
retains static findings separately from tool completion and unused-header
checks. New helper-header `pragma once` and internal parameter-order advisories
were inspected. The new missing direct test include was fixed as described
above; the frozen inventory still records it. These inventories are not a
warning-free tree claim.

## Full comparison protocol

The fixed selection contains 200 QF_LIA, 300 QF_UFLIA, 100 QF_LRA and 97 QF_UFLRA
inputs. It retains the original three predeclared QF_UFLRA abort quarantines;
those exclusions are not passes, and no new cases are removed. Selection SHA256:
`c76acbe0c55381a0e4db6a141427d5e8cc5daa59803fa0810af0b8e8640077c9`.

The control is the pre-instrumentation native source `07ec5ce5`, equivalent to
main `abe24b92`, binary SHA256
`f9f4468aa55a35eb10e6800711c7f92b361f0107ec301374092968e83e5a5fc4`.
This measures the net filters plus instrumentation cost. It differs from the
instrument-only control used for mechanism ablations.

Each of three rotated paired repetitions measures both binaries in both
profiles: 8,364 native rows at 120 seconds and an 8 GiB address-space limit.
SLURM allocations request one CPU and 10 GiB. Timings run in three sequential
arrays; models start after the last timing array. Every successful SAT timing
row receives a separate same-option replay with `--check-model` and direct
Dolmen validation, excluded from speed measurements. Observed logical CPU masks
are retained; physical-core isolation is not established. PAR2 charges every
unsolved row twice the time limit, including errors after an answer was printed.

Timing arrays `396982`, `396983`, `396984` and model arrays `396985`, `396986`,
`397083` all completed successfully. Submission followed terminal static job
`396962`. All 8,364 timing rows pass provenance and exit/status checks; every
one of the 4,515 successful SAT rows has a separate direct Dolmen pass.

## Full results and decision

The filters are retained in opt-in row mode. On common cases solved in all
three repetitions with baseline medians at least one second, the geometric
mean of per-case median paired runtime ratios is 0.928 (`rows`, 232 cases) and
0.925 (`both`, 238 cases): about 7.2% and 7.5% lower runtime within that eligible
population. This excludes cutoff-sensitive cases and faster cases, and is not
a speedup over all 697 inputs. QF_UFLIA contributes the clearest improvement:
ratios 0.868/0.863 across 107/109 eligible cases. QF_UFLRA is effectively neutral. Table cells show baseline → candidate; PAR2
is the median of the three repetition means. Paired ratios below one are faster.

| Logic | Profile | Median solved | Mean PAR2 (s) | Eligible pairs | Paired ratio |
| --- | --- | --- | --- | --- | --- |
| All four | rows | 539 → 539 | 58.291 → 58.153 | 232 | 0.928 |
| All four | both | 541 → 542 | 57.746 → 57.425 | 238 | 0.925 |
| QF_LIA | rows | 102 → 102 | 121.179 → 121.180 | 45 | 0.981 |
| QF_LIA | both | 102 → 103 | 121.563 → 120.964 | 44 | 0.982 |
| QF_UFLIA | rows | 260 → 260 | 35.177 → 34.909 | 107 | 0.868 |
| QF_UFLIA | both | 260 → 260 | 35.116 → 34.824 | 109 | 0.863 |
| QF_LRA | rows | 86 → 86 | 40.397 → 40.238 | 39 | 0.965 |
| QF_LRA | both | 88 → 88 | 36.034 → 35.841 | 44 | 0.965 |
| QF_UFLRA | rows | 91 → 91 | 18.585 → 18.578 | 41 | 1.000 |
| QF_UFLRA | both | 91 → 91 | 18.588 → 18.581 | 41 | 0.999 |

PAR2 improves by only 0.24% (`rows`) and 0.56% (`both`). Unsolved cases keep a
240-second penalty regardless of reduced work before the limit. These small
global changes do not establish a broad coverage improvement. Solved counts by
repetition are `539,538,539` → `539,539,539` for `rows` and `541,542,541` →
`542,542,542` for `both`.

Every paired solve difference is slot 17: the baseline times out in all six
profile/repetition measurements; the candidate finishes once in `rows` and
twice in `both`, at 118.624–119.286 seconds, but still times out in the other
three. Thus no case meets the strict stable-gain criterion of baseline failure
in all three repetitions and candidate success in all three. No measured pair
loses a solve. Cases 2, 213 and 597 remain timeouts in all twelve measurements
each. The exploratory consistent-slowdown screen finds none in its 232/238
eligible cases; that is not a claim about every subsecond case or a formal
significance test. Smaller timing differences remain in the exported data.

Across all 8,364 attempts, there are 4,515 successful SAT and 1,968 successful
UNSAT results, 768 normal unknowns (all diagnosed private integer node limits),
49 handled `std::bad_alloc` exits and 1,064 timeouts. The timeout count includes
two allocation-diagnostic exits and one late printed SAT answer; all remain
unsolved for scoring and are excluded from successful-SAT validation selection.
There is no unhandled crash, wrong pinned answer or invalid selected model in
this admitted selection. The three earlier quarantined abort cases remain
excluded, as stated above.

Median per-run RSS, taking the median of repetition medians, is 16,860 → 16,908
KiB for `rows` and 16,916 → 16,860 KiB for `both`. Maximum observed RSS is
7,773,776 → 7,759,352 KiB and 7,767,828 → 7,768,652 KiB respectively. These are
small changes and include resource-limited measurements; they do not demonstrate
a general memory improvement. Paired case RSS values are retained separately.

```{figure} ../images/propagation-filters.png
:name: propagation-filter-cactus

Stable-case cactus curves for the four affected logics. Each curve uses cases
solved by that arm/profile in all three repetitions and their median wall time;
intermittent cases are excluded. Legend counts therefore differ from median
solved counts in the table. The curves mostly overlap outside QF_UFLIA.
```

## Why the effect is selective

All 3,240 paired measurements where both arms complete successfully have
identical checked SAT decisions/conflicts, arithmetic pivots, theory conflicts,
private integer nodes and delivered row propagations. The filters add no inference rules; these matching counters show no search-size
change in the completed pairs.

The following counts pool successful candidate measurements with printed
statistics over all three repetitions. They exclude interrupted and unknown
runs, count repeated operations rather than distinct constraints, and describe
different completed cohorts in the two profiles.

| Profile | Successful measurements | Candidate attempts | Suppressed duplicate attempts | Requested row scans | Repeated-state skips |
| --- | --- | --- | --- | --- | --- |
| rows | 1,617 | 657,790,964 | 521,564,449 (79.3%) | 384,685,085 | 81,313,632 (21.1%) |
| both | 1,626 | 781,040,245 | 633,569,465 (81.1%) | 423,509,154 | 93,044,087 (22.0%) |

The Wisa case (slot 438) provides a complete-run example: `rows` median wall
time falls from 2.775 to 2.129 seconds, with median paired ratio 0.767. Each
candidate run suppresses 795,120 of 955,818 attempts and skips 115,458 of 331,606
row requests. With preprocessing, medians are 0.471 → 0.385 seconds. Core search
counts match throughout.

The newly cutoff-sensitive case 17 is also doing substantial arithmetic work:
one successful `both` run records 133,027,719 attempts, 119,854,085 suppressed
duplicates and 13,005,472 skips among 45,859,926 row requests. It takes no private
integer branch nodes. By contrast, case 64 remains around 38.5 seconds with
2,118 private nodes; its private-completion timer accounts for nearly all its
wall time. The filters do not address that case's dominant cost. This explains
why a real reduction in queue/scanning work need not substantially improve
PAR2 or solve many more instances.

The {download}`full comparison <../../benchmarks/propagation-redundancy-20261115/full/comparison.json>`
contains every repetition, logic/family summaries, paired slow-case times and
RSS, counter coverage and resource-diagnostic hashes. The
{download}`measurement table <../../benchmarks/propagation-redundancy-20261115/full/measurements.csv>`
retains all 8,364 rows and selected mechanism counters. No new source failure,
model failure or material screened regression required another timing campaign.
No default-profile promotion or default-performance claim is made.

## Reproduction and artifacts

The {download}`frozen protocol <../../benchmarks/propagation-redundancy-20261115/filter-protocol.json>`,
{download}`mutation audit <../../benchmarks/propagation-redundancy-20261115/final-mutations.json>`
and {download}`regression audit <../../benchmarks/propagation-redundancy-20261115/final-seeds.json>`
are committed with the evidence. Audit, prefix and hard-probe receipt files in
that directory identify each older source independently.

Use `scripts/slurm_prepare_benchmark_workspace.py --build-via-slurm` for a frozen
build, then the `bound_repairs` qualification controllers. Fresh seeds are
`2026112900` through `2026112907`, extra generated seeds `2026112908` through
`2026112911`, and mutation seeds `2026113000` through `2026113007`. Corpus roots
and SSH configuration remain private. The `propagation_redundancy` analysis
scripts check native/config/input provenance, successful-exit semantics,
complete counter partitions and per-row model coverage. The
{download}`run log <../run-log.json>` records commands, job IDs, resource
classification, raw-artifact locations and documentation review.

The {propagationreporttools}`full analyzer <scripts/qualification/propagation_redundancy/full_analysis.py>`
emits solved/PAR2 summaries, per-case paired times and RSS,
and counters summed only over successful rows with printed statistics.
Unsupported historical counter namespaces remain null; interrupted counts are
not included in these sums. Among cases solved by both arms in all three repetitions, paired details
cover those with either arm's median at least one second. Within that
population, the exploratory slowdown screen requires at least 10% and half a
second of added wall time in every paired repetition. This is
an investigation threshold, not a significance test or a reason to hide smaller
regressions; the full measurements remain available.

The {propagationreporttools}`cactus renderer <scripts/qualification/propagation_redundancy/plot.py>`
has the optional plot-only dependency
Matplotlib (the rendering environment uses 3.11.2). Qualification and analysis
otherwise use the Python standard library. Its four-panel cactus plot uses
per-case median wall times only for cases solved in all three repetitions by
that arm/profile. Legend counts therefore measure stable solves, rather than
the median solved counts reported in tables. It deliberately excludes
intermittent solves from the curves.
