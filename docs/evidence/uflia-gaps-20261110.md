# Why QF_UFLIA trails cvc5: selected gaps and profiles

**Investigation complete; no solver change or default promotion.** The uniform
120-second results show Primo solving 577/659 QF_UFLIA inputs against cvc5’s
607. There are 31 cvc5-only solves and one Primo-only solve. On the 576 inputs
both solve, the recorded Primo/cvc5 geometric-mean runtime ratio is 2.575.
These are byte-matched archived reference comparisons, not same-day paired runs.

The QF_LIA lead does not contradict this result. Its 13,306-input corpus is a
different collection, and its displayed Primo curve selects the integer
profile. QF_UFLIA performs best with defaults. The earlier
{ref}`integer-profile evaluation <qf-uflia-m124-review>` already
found that stronger integer bounds can worsen mixed-theory search. Neither
comparison measures the cost of adding UF to the same LIA input.

## Where the gap is concentrated

The {download}`ranker <../../scripts/qualification/uflia_gaps/rank.py>` reads
the qualified paired timing table and checks its identity against the export
provenance. It selects cvc5 successes where Primo is unsolved or at least ten
times slower. There are **107 such inputs**, including all 31 cvc5-only solves.

| Family | Inputs | Primo solves | cvc5 solves | cvc5 only | Primo only |
| --- | ---: | ---: | ---: | ---: | ---: |
| Certora | 76 | 8 | 26 | 18 | 0 |
| MathSAT | 454 | 441 | 454 | 13 | 0 |
| Wisas | 108 | 107 | 106 | 0 | 1 |
| TwoSquares | 21 | 21 | 21 | 0 | 0 |

Certora abbreviates `20230314-Jaroslav-Bendik-Certora`. The 13 MathSAT misses
are eight `EufLaArithmetic/medium` and five `hard` inputs. The Hash family also
has large runtime gaps even though both solvers finish. For example,
`hash_sat_10_19` takes Primo 51.185 s against archived cvc5’s 1.314 s;
`hash_sat_09_20` takes 34.103 s against 1.378 s.

## Fresh selected diagnostics

Six inputs were replayed using the unchanged `7dd8874c` binary, SHA256
`9f04d52de397d03b65439b18b35d172e55cffb037a16340860f33a9f9fb19318`.
Current production C++/headers/CMake still match that snapshot. Each input
runs defaults and the existing `--qf-uflia-tableau-equalities` option, rotating
which goes first. This is a mechanism control, not a new implementation.

| Input, shortened | Default | Equality sharing | Archived cvc5 |
| --- | ---: | ---: | ---: |
| MathSAT `medium13` | T | 0.938 | 0.049 |
| MathSAT `medium12` | 2.574 | 0.763 | 0.042 |
| MathSAT `hash_sat_10_19` | 48.022 | 45.750 | 1.314 |
| Certora `38347…_58` | T | 6.700 | 1.051 |
| Certora `39657…_48` | 24.487 | 23.652 | 1.108 |
| MathSAT Wisa `xs-09-06-1-5-3-2` | 8.503 | 0.184 | 0.026 |

Times are seconds; T means unsolved at the 120-second cutoff. Exact paths,
hashes and arguments are in the {download}`case manifest
<../../benchmarks/uflia-gaps-20261110/cases.json>`. These are single diagnostic
runs. Small timing differences, such as the two Hash arms, do not qualify an
optimization. Equality sharing’s earlier
[full-corpus result](qf-uflia-equality-sharing.md) remains mixed: it recovered
all 13 MathSAT misses but lost more native solves elsewhere than it gained.
The flag remains off by default.

### 1. Late equality exchange can trigger a large arrangement fallback

`medium13` reaches the 512 model-decision budget and then materializes all
30,135 interface pairs. The partial timeout statistics report 31,519,014
online arithmetic checks. Integer completion accounts for only 0.032 s;
this is not a difficult integer-rounding problem.

With equality sharing, the solver exports 704 explained equalities, takes
62 model decisions and materializes 766 pairs, with no complete fallback.
It finishes UNSAT. The 6,340-byte input defines a small arithmetic/function table:
224 of its 235 lexical UF applications have numeral arguments. Its three
integer constants are not a large arithmetic optimization problem.

The [existing equality integration](../implementation/uflia-equality-propagation.md)
exports proved consequences before integer completion. The
{patchsource}`component driver <src/smt/component_solver.cpp>` owns the decision
budget and full fallback. These measurements reproduce the mechanism described
in the [earlier larger comparison](qf-uflia-equality-sharing.md#mathsat-avoids-an-expensive-arrangement-fallback).

**Opportunity:** schedule useful explained equality batches before a costly
fallback or integer completion, while limiting unnecessary fresh equality
names and bridges. The [equality-exploitation lead](../leads/arithmetic-equality-exploitation.md)
records why unconditional naming also regresses search. The next experiment
should count already-registered versus fresh consequences, subsequent useful
EUF merges/conflicts and downstream pivots, using `medium12/13`, the two
Certora cases and the earlier Wisas regressions as controls. Explanations must
retain their true SAT premises; a coincident model value is not a proof.
Complete model reconciliation and arrangement fallback remain necessary.

(uflia-gap-hash)=
### 2. Hash spends time in repeated rational tableau work

On `hash_sat_10_19`, defaults record 998,958 online checks, 123,377 pivots,
17,314,853 affected-row updates and 214,134,568 pivot term operations.
Integer completion takes only 0.062 s; every recorded completion is initially
integral. Preprocessing takes 0.093 s. Improving patching or adding stronger
integer branching does not address this measured trace.

In the 35-second native diagnostic prefix, 172/225 stack observations contain
`LraTableau::check_impl`, and 71 contain `pivot`. These are inclusive wall-clock
observations, not CPU percentages. A separate 35-second Callgrind prefix places
11.07% of its self instruction count in `update_violation_status`, 10.24% in
rational multiplication, 7.29% in small-rational reduction and 6.74% in `pivot`.
The instrumented prefix is not the completed native run.

The {patchsource}`violation updater <src/theory/arithmetic/lra_tableau.cpp#L2064-L2081>`
computes bound status and updates a `std::set`; pivot/update operations call it
on affected variables. The
{patchsource}`online execution <src/smt/solver_engines.cpp#L9150-L9260>` already defers
checks to SAT quiescence points. Deferring every literal again is not missing
work. Also, each fresh interface batch recreates online execution through
{patchsource}`LiaTheoryComponent::register_atoms/reset <src/smt/solver_engines.cpp#L10391-L10422>`.
There are 237 rebuilds in this trace. Direct growth time is only 0.638 s;
whether losing the working basis causes substantial later repair is an open
hypothesis, not measured causation.

**Opportunity:** first instrument violation membership transitions and work
per pivot/check since interface growth. Test a narrow cache of unchanged
violation membership while preserving the exact ordered violated set. A
separate experiment can investigate retaining the working tableau across atom
growth, with explicit bounds, trail, reason and phase invalidation rules.
The [arithmetic efficiency leads](../leads/arithmetic-efficiency-followups.md)
cover row representation and coefficient access. These profiles raise
violation maintenance’s priority for this family; they do not justify changing
pivot order or replacing exact arithmetic without a controlled comparison.

Two single-run controls reinforce the boundary: disabling bound propagation
finishes Hash in 39.293 s, and strong preprocessing in 31.227 s; both models
pass Dolmen. Both remain far behind cvc5. Their search and pivot counts change,
so neither difference isolates direct propagation or preprocessing overhead.

(uflia-gap-certora)=
### 3. Certora has both integer-search and rational/combination costs

On `38347…_58`, the default times out. Its 35-second prefix has integer
branch-and-bound in 126/225 observations and GMP in 112; these categories
overlap. Equality sharing finishes in 6.700 s with no integer branch nodes.
Disabling bound propagation also finishes, in 12.089 s, with a validated model.
This supports investigating search/inference scheduling rather than claiming
that the integer arithmetic library alone explains the gap.

On `39657…_48`, equality sharing barely changes the completed runtime.
The default has 22/161 observations in
{patchsource}`EUF care-pair construction <src/smt/solver_engines.cpp#L3014-L3103>`,
34 in pivoting and 25 containing GMP. The next care-pair experiment should
measure closure/version reuse and allocation cost while preserving the exact
deterministic returned pairs. The [care-pair lead](../leads/care-pair-generation.md)
distinguishes this bookkeeping experiment from changing which equalities may
be omitted. A cache must invalidate on relevant merges, rollback, term growth
and model-token changes.

The small Wisa control illustrates another avoidable commitment: defaults
spend 7.900 s in integer completion, reach 100,000 branch nodes and fall back;
equality sharing finishes in 0.184 s, and disabling bound propagation in
0.152 s. These are different search traces. They motivate bounded inference
and scheduling experiments, not a blanket recommendation to disable propagation.

## Correctness finding: strong preprocessing is not an accepted remedy

All seven successful SAT timing rows from the default/equality-sharing pair
pass separate direct Dolmen validation. Five of six additional control SAT
rows pass. The remaining strong-preprocessing control, Certora `38347…_58`,
returns SAT during timing but exits 2 during model replay:

> model: assembled component model falsifies a final assertion

The {patchsource}`model assembler’s final assertion guard
<src/smt/component_model_assembler.cpp#L569-L578>` rejects the assembled model
before external validation. This is a model-construction/checking failure,
not a confirmed wrong SAT decision or a model accepted by Dolmen. The equality-sharing model passes on the same input. The default times out
and supplies no SAT row for model validation there.
Strong preprocessing is excluded from accepted performance recommendations;
no further tuning was launched after discovering the failure.

The {download}`failure record <../../benchmarks/uflia-gaps-20261110/model-replay-failure.json>`
preserves its input/binary identities, options, diagnostic and coverage digest.
The failing assertion and underlying reconstruction defect have not been
minimized or fixed. Repair and independently validate this profile before
using it in further performance claims. No solver implementation or default
was changed by this investigation.

## Reproduction and evidence boundaries

All solver work ran through SLURM using preparation-helper preflight against
the clean frozen source. Each native timing uses one physical core and an
8 GiB hard address-space limit. Tasks request 12 GiB to accommodate diagnostic
tools. GDB has the same inherited per-process limit; Callgrind has the SLURM
memory limit without a separate address-space cap. Their durations never enter
native timings. Each case’s instrumented runs follow its own native timings.

The six-task primary model array depends on the complete primary profiling
array. The separate six-task control model array depends on all control
timings. The control timings overlapped part of the primary model-validation
stage on separately allocated cores; this investigation makes no repeated
paired speedup or default-promotion claim. Model replay uses 180-second solver
and 60-second Dolmen limits, with Dolmen size 5G. References were not rerun.
There are 18 timing rows: 13 SAT, three UNSAT and two timeouts; no timing
crashes, unsupported inputs or concrete reference disagreements. Of 13 selected
SAT replay rows, 12 pass and one has the explicitly rejected solver error.

- {download}`Ranked gaps and family totals <../../benchmarks/uflia-gaps-20261110/gaps.json>`
- {download}`Fresh counters, profile observations and coverage identities <../../benchmarks/uflia-gaps-20261110/diagnostics.json>`
- {download}`Input shape counts <../../benchmarks/uflia-gaps-20261110/input-shapes.json>`
- {download}`Diagnostic runner <../../scripts/qualification/uflia_gaps/run.py>` and {download}`batch file <../../scripts/qualification/uflia_gaps/run.sbatch>`
- {download}`Control runner <../../scripts/qualification/uflia_gaps/controls.py>` and {download}`artifact auditor <../../scripts/qualification/uflia_gaps/summarize.py>`

The run ledger records jobs, commands, limits and outcome counts. Raw timings,
profiler traces, Callgrind files and model responses remain in the private
SLURM archive and under `results/uflia-gaps-20261110` locally. The executed
control capsule is retained there; the checked-in owner has only a subsequent
lambda-to-named-function lint cleanup. These are focused diagnostic leads,
not a new corpus qualification, algorithm implementation or default promotion.
