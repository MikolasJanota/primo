# Redundant arithmetic propagation work

This experiment distinguishes repeated work from newly derived constraints.
The instrument-only audit found substantial duplication in the pending
SAT-literal queue and repeated scans after bounds that did not strengthen the
tableau. The implementation filters those two operations under the existing
opt-in row-propagation option. It adds no arithmetic rows, variables, atoms or
inference rules. Option defaults are unchanged.

The final candidate is frozen at `c815a0b4`; its native SHA256 is
`3a40b72a4837017a1b3d39dc70e0454b9c39796975d9e66f56271bd61c41e9ed`.
Main implementation commit `b62e9649` has identical production source; its
unit test adds only the direct `sat/lit.hpp` include after the frozen scan,
with the test body unchanged and a successful bounded syntax-only compile.
Merge `7e16e7d6` preserves all measured snapshots in main's ancestry without
changing its tree. Source correctness and static qualification are complete;
the full paired comparison is running. This page does not yet claim an overall
performance improvement.

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
`397083` were submitted after static job `396962` became terminal. Full timing,
validation, regression analysis and performance acceptance remain pending.

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
