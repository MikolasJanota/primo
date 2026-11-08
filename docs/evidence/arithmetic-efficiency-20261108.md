(arithmetic-efficiency-review)=
# Arithmetic efficiency review, 8 November 2026

**Status: focused review complete; arithmetic behavior and defaults unchanged.**
The review uses Bromberger's *Decision Procedures for Linear Arithmetic* (2019)
{cite:p}`bromberger2019`, especially Chapters 7–8. The
[reading checklist](../papers/bromberger-linear-arithmetic.md#efficiency-review-checklist)
separates implemented mechanisms, missing parts and proposed experiments.

The most actionable costs are repeated patching calculations, oversized Boolean
encoding on `nec-smt`, shared-model regrouping, and large-rational normalization.
These need different remedies; the profiles do not support treating all slow
arithmetic inputs as the same branching problem.

## Findings in priority order

### 1. Cache freedom intervals during patch candidate selection

{cubesource}`collect_candidates <src/theory/arithmetic/lia_patching.cpp#L218-L273>`
computes `tableau.freedom_interval(moved)` inside its per-candidate callback.
A basic target offers eight nearby integer targets and potentially eight lattice
targets for the same moved variable. Other basic targets can offer that variable
again. The tableau is unchanged throughout candidate selection; only the chosen
move at the end of the attempt changes assignments.

On case 4, `convert-jpg2gif-query-1577.smt2`, normal native runtimes are 4.30 s
with defaults and 3.96 s with M124 plus uniform cubes; Z3 takes 0.24 s. M124
means bound rounding, primitive integer rows and integral patch moves enabled,
with integer-slack obligations disabled. Native stack samples contain freedom
interval calculation in **25/43** default samples and **24/39** M124 samples.
Patching itself occurs in 38/43 and 34/39 respectively. These are inclusive
sample memberships, not additive time percentages.

**Proposed repair:** cache each moved variable's interval for one candidate
selection attempt, invalidate after every accepted move, and preserve candidate
order and scoring. Rejecting a nonintegral candidate before an interval query
is another possible fast path; its rejection-counter definition would need
updating. The current scoring index already avoids a full integer-row scan;
that improvement should be retained.

A focused test should make several targets share one moved variable, exercise
both candidate families and an accepted move that changes another interval.
Count cache queries/misses and compare candidate choices, assignments and bounds
with the existing implementation. The paper's §7.1.4 principle is incremental
maintenance of expensive derived information; this particular patching cache is
our implementation inference, not a named thesis algorithm.

### 2. Make post-rewrite Boolean flattening sensitive to the input shape

{cubesource}`flatten_associative_booleans <src/smt/preprocessing.cpp#L3455-L3530>`
expands same-kind children up to the arity cap, without a sharing criterion.
The {cubesource}`default post-rewrite cap <src/smt/check_sat.hpp#L428-L448>` is
2048. On case 0, `nec-smt/large/checkpass/prp-23-46.smt2`, this produces
**8,723,275 clauses**, including 8,535,798 binary clauses. Its native timers
report 11.24 s in preprocessing, including 5.65 s in Boolean normalization and
2.92 s in constant if-then-else (CITE) simplification. Native GDB samples contain Boolean
flattening in 53/236 samples, CITE simplification in 31/236 and CaDiCaL
in 94/236. Only one sample contains a tableau operation; none contains integer
completion.

A separate, repeated existing-option experiment isolates the cap. Every cell
below contains the two normal runtimes in seconds; a timeout remains unsolved.

| Case | Cap 0: disabled | Cap 256 | Cap 2048: default |
| --- | ---: | ---: | ---: |
| 0: NEC `prp-23-46` | 16.80 / 16.31 | **12.88 / 12.19** | 24.76 / 24.87 |
| 1: NEC `prp-6-49` | 32.84 / 32.65 | **31.14 / 29.49** | timeout / timeout |
| 6: LassoRanker control | 6.11 / 6.16 | 1.17 / 1.16 | **1.16 / 1.14** |
| 11: Labyrinth control | 53.48 / 52.36 | 49.57 / 50.16 | 55.01 / 48.72 |

For case 0, the clause counts are 1,604,147 with cap 0, 2,318,322 with cap 256,
and 8,723,275 with cap 2048, identical across repetitions. Thus cap 256 almost
halves runtime on this case while preserving enough flattening to help the
LassoRanker control. Case 11 shows substantial runtime variation with unchanged
clause counts; it does not distinguish the caps reliably.

**Next experiment:** retain shared tails, or choose a cap from measured sharing
and encoding growth. The thesis's §8.2.3 compression explicitly stops at shared
tails, and §8.2.5 describes the interaction of CITE simplification and compression
on NEC. That motivates the experiment; Primo does not implement the full thesis
pipeline. The four-case experiment does not qualify a global default change.
The existing 2048 setting also has a broader historical LRA tuning rationale.

### 3. Maintain shared-value class counts during trial model mutations

On case 10, the QF_UFLRA BMC `parport` input, normal native time is 5.48 s
versus Z3's 1.16 s. **38/55 native samples** contain `group_by_model_value`
inside {cubesource}`diversify_shared_model <src/theory/arithmetic/lra_tableau.cpp#L1554-L1713>`.
The run reports 15 mutation calls, 1,291 accepted updates and 260,008 pruned
candidate pairs, so the mechanism is doing useful work.

Each trial move nevertheless reconstructs a hash-based grouping over the whole
shared-variable list and allocates class vectors. Rejected trials then restore
the model and repeat the work for the next value. The helper is linear in the
shared list per call; the repeated calls are the issue.

**Proposed repair:** keep exact value-bucket counts in the scratch model, update
only shared values affected by a trial move, undo rejected moves, and construct
class vectors once when needed for export. Preserve class counts including
singletons, forbidden-equality checks and deterministic candidate/output order.
A probe should vary total shared variables while holding the affected column
small, with successful and rejected moves. The existing
[mutation contract](../implementation/combination.md#when-model-mutation-runs)
remains the correctness basis; this is a bookkeeping optimization, not the
thesis's complete equality-basis procedure.

### 4. Investigate common row denominators for the large-coefficient cases

The ring and transformed-cut profiles locate most work in numerical tableau
operations. The following counts are inclusive native stack memberships in
40-second diagnostic runs; timed-out samples describe prefixes.

| Case/profile | Samples | Pivot | Update | Gomory | Branching | GMP |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 2 rings, default | 376 | 248 | 113 | 10 | 355 | 343 |
| 2 rings, M124 + cube | 94 | 55 | 31 | 4 | 86 | 77 |
| 3 preprocessed rings, default | 376 | 325 | 46 | 4 | 367 | 342 |
| 3 preprocessed rings, M124 + cube | 375 | 311 | 35 | 273 | 83 | 345 |
| 5 transformed cut, default | 377 | 335 | 26 | 363 | 0 | 373 |
| 5 transformed cut, M124 + cube | 374 | 319 | 31 | 359 | 0 | 373 |

Case 2 times out with defaults and finishes in 9.28 s with M124 plus cubes;
Z3 takes 0.027 s. Cases 3 and 5 time out under both native configurations while
Z3 takes 0.027 and 0.051 s. In particular, the transformed-cut prefix is still
in Gomory processing, before branching. A branching policy acts later than
this measured bottleneck.

Case 2's default Callgrind prefix assigns 23.82% of self instructions to `div2`
and 22.59% to `__gmpn_hgcd2`. These instruction shares describe that instrumented
prefix, not the whole native runtime. Together with the native GMP samples,
they make repeated big-number normalization a concrete target.

Bromberger §7.1.4, pp. 222–225, stores integer numerators and one common
positive denominator per row. Primo already has sorted sparse merging and
indexed column deletion, but {cubesource}`pivot <src/theory/arithmetic/lra_tableau.cpp#L143-L335>`
still performs arithmetic on individually normalized rational coefficients.
Implementing the row representation requires exact scaling, normalization,
occurrence maintenance and SOI rollback checks, as listed in the reading guide.
No speedup from that representation has yet been measured in Primo.

Primo already has a 64-bit inline/GMP fallback representation. Switching
libraries would not supply a missing small-number fast path. The tropical
matrix stress control, case 7, instead has 336/375 samples containing pivoting
and **zero GMP samples**; both Primo and Z3 time out there. It illustrates a
different small-rational/row-processing cost and is not a fast-reference gap.

### 5. Avoid scanning every CITE leaf to decide a constant comparison

{cubesource}`csimp_against_constant <src/smt/preprocessing.cpp#L3953-L4010>`
counts every true leaf, but then uses only the distinction between all true,
all false and mixed. The cached leaf sets are sorted and unique. For `<` and
`<=`, their endpoints determine uniformity; equality with a constant needs a
membership test and the singleton case. This permits constant or logarithmic
comparison work instead of a full scan on each distinct cache miss.

CITE work appears in 31/236 complete native samples on case 0 and 58/386
capped samples on case 1. Rational comparison and CITE functions are also
prominent in the early Callgrind prefixes. Those profiles do not attribute every
rational comparison to this particular loop. A counter for leaf comparisons
and a large-leaf uniform/mixed probe should establish the local saving.
Keep the full leaf sets for equality reasoning and use exact comparisons,
including negative/zero scales and strict relations.

## Other paper-backed leads and limits

- The {ref}`rounding example <simultaneous-rounding-gap>`
  shows why feasibility-preserving patching does not subsume the thesis's cheap
  simultaneous rounding test. Its benefit on the selected cases is unmeasured.
- Internal branch nodes currently assert a split and run simplex. They do not
  run the thesis's bounded numerical propagation and rounding loop. Propagation
  restricted to existing SAT literals is a different mechanism.
- `update` and freedom-interval queries still search affected rows for their
  coefficients. The [column coefficient lead](../leads/lra-column-coefficients.md)
  already records this; caching repeated intervals is the smaller first change.
- Bounds-only `pop` still rebuilds the violated-variable set globally. These
  fresh profiles do not show that rebuild dominating, so it is a lower priority
  than the measured costs above.
- The full CITE/ITE reconstruction, shared-monomial lifting and leaf-bounding
  pipeline is absent. Thesis §8.2.5 makes it relevant to the ring families,
  but its SPASS-SATT gains do not predict Primo's gains.
- The complete equality basis and Chapter 6 bounding transformations remain
  larger, unimplemented work. A finite branch budget supplies termination with
  `unknown`, not a complete LIA decision procedure.

The two QF_UFLIA controls take 1.62 s and 0.16 s natively. Their 16 and two
native samples are too few for a new broad hotspot claim. Historical equality
sharing regressions retain their separate evidence. None of these selected
controls is a representative corpus evaluation.

## Method, provenance and verification

The profiled Release binary is source `84654ade`, SHA-256
`0102030d508c15a51ee3d2f35a5ff19aa5d0183b396f65aa8d7796cfcda8d495`, with
CaDiCaL `3.0.0-7b99c07`. Its C++ source matches review baseline `44bb99c1`.
The subsequent `bf889d32` fix changes only the qualification runner, regression
and documentation. Each input has a recorded content hash.

SLURM `365599` ran 12 selected cases, four concurrent tasks, one CPU and 24 GiB
requested per task. It records 18 native option profiles and 12 Z3 runs at a
60-second normal timeout. Normal runtimes are separate from GDB's jittered
0.1-second stack samples and capped 40-second Callgrind instruction runs.
Nine GDB runs exit normally and nine reach the sampler deadline. Capped profiles
are useful prefixes, not completed searches or speed measurements. Inclusive
memberships overlap, and inclusive Callgrind percentages must not be summed.

SLURM `365629` ran the four-case cap ablation with two rotated repetitions on
one CPU and 12 GiB per task, a 60-second timeout and fresh Z3. Every successful
native SAT row was separately replayed with a 90-second solver budget and
30-second Dolmen budget. **All 10 SAT profile/repetition rows pass direct
Dolmen validation**, covering four distinct inputs. No wrong answer, process
error or contradictory successful decision was found in the completed runs.
The initial submission `365583` failed before solver execution because of a
tool-version probe and a partial-corpus path; it supplies no performance evidence.

The relative-root manifest-runner review issue is fixed in `bf889d32`. Its
focused actual-runner regression uses mocked solver/version calls, so it runs
no local solver. Arithmetic code is unchanged; this review does not rerun or
claim new full solver qualification. The prior exact-source correctness gates
and 135-file static scan are retained in the cube qualification metadata;
that scan has baseline advisories and is not warning-free.

Reproduction methods:

- {download}`Profile batch <../../scripts/qualification/arithmetic_efficiency/profile.sbatch>`;
  {download}`existing-option ablation batch <../../scripts/qualification/arithmetic_efficiency/flattening.sbatch>`.
- {download}`Audit/analysis script <../../scripts/qualification/arithmetic_efficiency/analyze.py>`;
  {download}`frozen selection and resource metadata <../../benchmarks/arithmetic-efficiency-20261108/experiment.json>`;
  {download}`audited profile/timing/model summary <../../benchmarks/arithmetic-efficiency-20261108/summary.json>`.

Private corpus/binary locations are supplied through `PRIMO_REVIEW_CONFIG`;
connection details stay outside tracked files. Start with `experiment.json`, add absolute `binary` and `z3` executable paths,
and add a `root` corpus directory to each case; retain the checked-in case order.
Set `PRIMO_WORKDIR` to the qualified workspace, `PRIMO_REVIEW_CONFIG` to that
private JSON, `PRIMO_REVIEW_SCRIPT` to the staged batch script, and
`PRIMO_REVIEW_OUT` to a fresh output directory. The profiling array is
`0-11%4`; the flattening array is `0-3%4`, selecting cases 0, 1, 6 and 11.
Use `scripts/slurm_prepare_benchmark_workspace.py` for build/source preflight
and `scripts/slurm_validate_sbatch.py` before each submission.
Raw profiles, model artifacts and accounting remain under
`results/arithmetic-efficiency-20261108`. The canonical run-log entry is
`2026-11-08-arithmetic-efficiency-review`.

```sh
python3 scripts/qualification/arithmetic_efficiency/analyze.py \
  --root results/arithmetic-efficiency-20261108/retry1 \
  --flattening results/arithmetic-efficiency-20261108/flattening \
  --experiment benchmarks/arithmetic-efficiency-20261108/experiment.json \
  --out benchmarks/arithmetic-efficiency-20261108
```
