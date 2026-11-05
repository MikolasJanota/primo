# Phase-region policy simulations

**Status: completed diagnostic; these policies are rejected for adoption on this evidence.**
This follows the [CSP reading](../papers/csp-restarts-phase-control.md) and
the [earlier phase diagnostic](adaptive-phase-probe.md). It is an original
bounded adaptation, not an implementation of finite-domain impact-based search.
Baptista §6.4.1 motivates varying complementary branch orientation; it does
not provide the geometric gate below or predict a Primo improvement.

## Results

All 1,452 timing attempts completed on SLURM (357085), followed by separate
SAT-model replay (357086). There were no errors, wrong answers, unsupported
results or reference disagreements. Every successful SAT row was accepted
directly by Dolmen; model time is excluded from the timing comparison.

Dolmen coverage was **393/393 SAT rows**.

The three loss columns use seeds 2026110531/532/533 and each have 30 cases.
Controls are 14 cases repeated three times. PAR2 is the wall-time sum over all
132 rows per arm, charging 30 seconds for every unsolved row.

| Policy | Losses, seed 531 | Seed 532 | Seed 533 | Controls /42 | PAR2 sum (s) |
|---|---:|---:|---:|---:|---:|
| Native | 30 | 30 | 30 | 42 | 99.6 |
| M124, β off | 0 | 0 | 0 | 42 | 2720.1 |
| M124 + β, no region controller | 0 | 0 | 0 | 42 | 2742.0 |
| Observe β | 0 | 0 | 0 | 42 | 2763.2 |
| Fixed positive, β off | 15 | 15 | 15 | 42 | 1589.6 |
| Random 1/16 | 0 | 0 | 0 | 39 | 2824.0 |
| Random 1/4 | 0 | 0 | 0 | 39 | 2828.0 |
| Revisit gate 1/16 | 0 | 0 | 0 | 42 | 2763.1 |
| Revisit gate 1/4 | 0 | 0 | 0 | 42 | 2763.4 |
| Prefer wider child | 0 | 0 | 0 | 42 | 2763.4 |
| Prefer narrower child | 0 | 0 | 0 | 42 | 2763.1 |

Revisit-gated flips occurred on only 12 of 90 loss attempts: four files
(`xs_24_44`, `xs_27_47`, `xs_39_59`, `xs_40_50`) in all three seeds.
The immediate wider/narrower controls changed preferences in all 90 attempts.
The gated negative result therefore covers a much smaller active subset.

No new policy recovered a recorded loss in any seed. This remains a useful
negative result for this particular interval/history signal, rather than a
rejection of adaptive phases in general. The fixed-polarity control still
recovers cases, so useful alternative search remains possible on this set.

The controller was active on the lost cases. Counts below cover the 90 loss
attempts per arm; repeated means history-qualified narrow contexts, before
checking whether β selects that child or drawing the random flip.

| Policy | Hint contexts | Narrow contexts | Repeated | Preference flips | Loss rows with flips /90 |
|---|---:|---:|---:|---:|---:|
| Observe β | 107,024,728 | 448,866 | 12,481 | 0 | 0 |
| Random 1/16 | 110,727,805 | 719,542 | 8,644 | 6,922,091 | 90 |
| Random 1/4 | 115,301,358 | 877,788 | 15,855 | 28,905,122 | 90 |
| Revisit gate 1/16 | 106,806,788 | 451,131 | 13,185 | 668 | 12 |
| Revisit gate 1/4 | 106,740,657 | 448,116 | 11,711 | 2,360 | 12 |
| Prefer wider child | 107,233,708 | 494,778 | 13,457 | 233,548 | 90 |
| Prefer narrower child | 108,653,149 | 537,331 | 15,069 | 307,766 | 90 |

Control timeouts were:

- `mathsat/Hash/hash_sat_07_15.smt2`, Random 1/16, seed 2026110531.
- `mathsat/Hash/hash_sat_07_15.smt2`, Random 1/16, seed 2026110532.
- `mathsat/Hash/hash_sat_07_15.smt2`, Random 1/16, seed 2026110533.
- `mathsat/Hash/hash_sat_07_15.smt2`, Random 1/4, seed 2026110531.
- `mathsat/Hash/hash_sat_07_15.smt2`, Random 1/4, seed 2026110532.
- `mathsat/Hash/hash_sat_07_15.smt2`, Random 1/4, seed 2026110533.

These are timeouts, not incorrect answers; they still count as regressions.

The immediate geometry controls remove the revisit condition and still fail
to recover a loss. Thus simply making this history gate fire earlier does not
have supporting evidence here. A small interval is not a demonstrated proxy
for difficult combined EUF/LIA search. Random perturbation without the geometry
gate does not rescue this sample either.

The prototype also adds overhead. In paired loss attempts that both timed out,
the median ratio of observed decision counts (observe divided by β without
region instrumentation) was 0.80 across 90 pairs. This is a
search-work diagnostic under the same wall limit, not a pure throughput ratio:
the extra refresh scheduling can also affect advice timing. It limits the
negative timing conclusion. Random contexts are sampled many times within a
run; probability 1/16 is sparse per context, not necessarily a small number
of total perturbations. Random and gated arms match that probability, not
their total override counts; this does not isolate gate information at a matched
actual override budget. Installed-sign alignment cannot establish how many
actual decisions changed relative to counterfactual β.

The prototype is retired from production. This preserves the useful β policy
while keeping these measurements and the original experimental source available.
No default, restart schedule, arithmetic bound or temporary assumption changed.

## State, invariants and operations

The {regionsource}`ArithmeticPhaseSimulation controller
<src/smt/arithmetic_phase_simulation.hpp>`, owned by
{regionsource}`ComponentLraExecution <src/smt/solver_engines.cpp>`, keeps phase
choices and integer-bound snapshots within that execution's lifetime. A
tableau variable keeps its prepared semantic meaning through pivots; all
history is discarded when the execution/preparation is rebuilt. β denotes
the current rational tableau assignment. The controller borrows
no terms or tableau references. Owned rational endpoints describe closed
integer candidate ranges, using the existing infinitesimal-aware floor/ceil.
This is an interval abstraction, not a count of combined EUF/LIA models.

At each phase refresh, record the current rounded bound range once per bound
variable. Entering a different finite range increments its visit count; repeated
refreshes in the same range do not. History is capped at 4096 FIFO entries.
An atom's sampled preference remains fixed until its bound-range generation
or β sign changes, or the atom is observed assigned at a refresh. Assigned
atoms receive β and lose their cached choice, so a later unassigned refresh
can resample even with the same bounds and β. Before the initial assignment
view is attached, only pending preferences are sampled; these are not observed
decisions. The policy cannot reverse a
propagated literal or a forced assumption. Initial preferences may be installed
before a future assumption is known, but assumptions retain their forced sign.

For a currently unassigned inequality on a known integer-valued expression,
intersect the parent range with each polarity's prepared bound. Require both
children to have positive finite candidate counts. A narrow child has at most
four candidates and the other has at least four times as many. The gate requires
at least two recorded entries into that narrow child's exact range for the
history-gated random policy. The two deterministic geometry controls omit
that history condition, separating geometry from repeated-entry detection.

For example, let the active integer range be `0 ≤ x ≤ 20` and the existing
atom be `x ≤ 2`. Its children have three and eighteen interval candidates.
The wider-child policy recommends the atom's false sign; the narrower-child
policy recommends its true sign. The history gate additionally needs two
recorded entries into the exact range `[0,2]` and β currently recommending
that true sign. A recommendation changes only this existing atom's phase;
it does not assert `x ≥ 3` or choose a particular value of `x`.

The policies are:

- observe: measure contexts without changing β;
- random: flip β with probability 1/d at a new hint context;
- gated: the same random flip only when β selects the revisited narrow child;
- least: whenever the narrow/ratio test holds, prefer the child with more candidates;
- most: under that same geometry test, prefer the child with fewer candidates.

The sparse denominators are 16 and 4. Random sampling uses C++'s standard
`mt19937` and uniform integer distribution, with explicit recorded seeds.
These are persistent context choices, not the thesis's once-per-node sampling.
The gate measures revisits, not proven lack of clause-learning progress.
Unbounded, unknown-integer and empty-child geometry cannot activate the gate.
Random controls can still perturb an inequality with no finite geometry.

No policy adds a bound, atom, assumption, variable choice, clause or restart.
SAT selection, learning and existing resource controls remain responsible for
search. Only sign delivery changes. Equality atoms remain outside this producer.
Every recommendation uses bounded container work and exact arithmetic; endpoint
comparison/formatting costs depend on rational operand sizes. Cached contexts
avoid repeated random draws and history formatting on unchanged bounds.

The earlier SAT-layer `PhaseProbe` is restored only as observe instrumentation:
it identifies actual observed decisions through the backend predicate and
separates propagations/assumptions. Its alignment is with the installed sign,
not counterfactual β. Arithmetic flip/context counts do not prove that a later
decision changed. Both streams are retained and reported with that limitation.

## Qualification and measurement

Build the clean snapshot through `slurm_prepare_benchmark_workspace.py
--build-via-slurm`. Run deterministic controller tests for strict/negative bounds,
finite/unbounded ranges, both semantic atom signs, history re-entry, FIFO limits,
stable sampling, assigned-literal exclusion and least/most/gated distinctions
in the {regionsource}`controller regressions
<tests/unit/arithmetic_phase_simulation_tests.cpp>`.
Run solver checks, ASan, a fresh mixed QF_UFLIA reference gate and whole-tree
static review on SLURM before accepting timing claims. Keep failures blocking.

Reuse the committed four development losses, all 30 recorded losses and 14
family controls from `scripts/qualification/adaptive_phase/selection.json`.
Compare native, M124 without β, β without the region controller, observe, sparse random,
gated random, least and most. Use 15 seconds per process, rotated arm order
and three recorded seeds for the randomized policies. This is a targeted
diagnostic, not a full-corpus or default-promotion evaluation. Validate every
successful SAT timing row separately with direct Dolmen acceptance.

Retain exact source/binary/input hashes, commands, resource limits, context and
decision counters, per-family outcomes and PAR2 in artifacts/run-log metadata.
After measurement, decide the prototype's disposition and restore production
source unless further qualification establishes a reason to adopt it.


## Qualified snapshot and run receipt

The release binary is frozen at `ade90f98d568bb8946a4490600f04a61b1e5de63`.
SLURM build 357080 completed successfully. Earlier builds 357077 and 357078
were superseded before qualification: the first lacked the initial assignment-view
guard, and the second preceded the immediate least/most geometry controls.
Neither supplies timing or correctness evidence for the final policies.

Checks job 357082 passed 47 CTest targets and 311 Python tests. Four fresh 1,000-case
mixed QF_UFLIA streams (random, gated, least and most; generator seeds
2026110541–2026110544) agreed with Z3, with no timeout, wrong answer, crash,
parser/type rejection, unsupported result or reference disagreement. The
2,024 successful generated SAT cases were independently accepted by Dolmen
(516/501/517/490 respectively). ASan job 357083 passed all 47 CTest targets and
four further 20-case streams, with no reported sanitizer failures. Both gates
stress random sampling at denominator 1; timing uses 4 and 16.

Static job 357084 completed its scan of 131 source/test translation units and
four documentation examples: 2,201 warning occurrences, 761 unique diagnostics,
zero errors. Baseline advisories remain. The controller adds parameter-swap and
guarded-optional-access advisories; its tests add exception/include advisories.
These are recorded in the ledger and are not described as a warning-free gate.
The 12-task timing array is 357085, followed by
SAT-model replay array 357086. All arms retain the SAT observer; `beta-plain`
omits the region controller and its extra dirty-bound/backtrack refresh work.
Thus it is a reference for that instrumentation and refresh scheduling, not a
measurement with identical callback work. Timing uses seeds 2026110531, 2026110532 and
2026110533 for every arm, including deterministic controls, for 1,452 completed
attempts. All 44 selected files are measured together; the four earlier
development files are a pre-existing marked subset, not a new parameter sweep.

The complete source delta is retained as
{download}`source.patch <../../scripts/qualification/phase_region/source.patch>`.
Production `CMakeLists.txt`, `src/` and unit tests are restored exactly to
`2eb17d8e`; the policies remain reproducible through the frozen source and patch. Raw local receipts and results are
under `results/phase-region-20261105/`; connection settings remain external.

## Reproduction and artifacts

Create an isolated worktree at `ade90f98` and build it with
`scripts/slurm_prepare_benchmark_workspace.py --build-via-slurm
--reuse-dependencies --jobs 4 --extra-target primo_arithmetic_phase_simulation_tests
--extra-target primo_phase_probe_tests`, supplying the session's external SSH,
workspace and C++ header-manifest settings. The frozen checkout contains the
qualification, timing and replay scripts. Run the helper's `--check-only
--no-sync` immediately before each submission. Use fresh output directories.

The scripts under `scripts/qualification/phase_region/` record the exact flags,
seeds and resource allocations. Run `check.sbatch`, `asan.sbatch` and
`static.sbatch` first. Then submit `run.sbatch` as array `0-11%12`, with
`PRIMO_PROBE_SHARDS=12`, the committed adaptive-phase `evaluation.txt` manifest,
and arms `native m124 beta-plain observe force-positive random-16 random-4
gated-16 gated-4 least most`. Submit `replay.sbatch` with the same array size,
depending on successful completion of the timing array. Export the workspace,
corpus root, expected revision and output variables required by those scripts
outside tracked files. Each timing row retains its full command and environment.

The current `summarize.py` verifies the complete file/policy/seed matrix,
qualified binary and script identities, each policy's command/environment,
and exact successful-SAT replay coverage. From the current checkout, analyze
collected artifacts with:

```bash
python3 scripts/qualification/phase_region/summarize.py \
  --timing results/phase-region-20261105/timing \
  --models results/phase-region-20261105/models \
  --selection scripts/qualification/adaptive_phase/selection.json \
  --expected-git ade90f98 \
  --expected-solver-sha256 40b861ab7a88c97a36b2240a58c209423a6ed8564195e8494a10db469f4c1f54 \
  --out results/phase-region-20261105/verified-summary.json
```

The frozen-source patch applies to `2eb17d8e` with `git apply --unidiff-zero`.
A rebuilt binary can have a different hash; bind analysis to its independently
checked qualification identity rather than copying the recorded hash above.
The committed
{download}`metadata <../../benchmarks/qf-uflia-phase-region-20261105.json>`
contains input/binary/script/row hashes, per-family outcomes, per-seed solved
sets, runtime distributions and full counter summaries. `docs/run-log.json`
records qualification, job IDs, resources, limitations and the rejected result.
Complete raw timing, generated inputs and replay artifacts are retained locally
under `results/phase-region-20261105/` and in the isolated SLURM workspace.
