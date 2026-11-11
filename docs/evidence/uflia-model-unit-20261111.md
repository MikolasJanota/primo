# Cheap equality discovery in QF_UFLIA

Primo now offers the model-unit equality producer from Bjørner and Nachmanson's
*Arithmetic Solving in Z3*, CAV 2024, §3.2 {cite:p}`BjornerN24`, for shared
integer terms. Its correctness qualification is complete. Performance is mixed;
it remains an explicit research option. Equality export stays off by default,
and `legacy` remains the producer default.

```console
primo --qf-uflia-tableau-equalities=all \
      --qf-uflia-lia-equality-producer=model-unit input.smt2
```

The selector alone does not enable export. The
[implementation checklist](../implementation/uflia-equality-propagation.md#cav-2024-model-unit-discovery-integration-checklist)
maps the paper to the existing tableau query and the integer adapter. Equal
current values filter candidates; fixed bounds and unit-row identities justify
the proofs. No general integer equality procedure or implementation of every
technique in the paper is claimed.

## Implementation boundary

`LiaComponentAdapter` selects the existing owning
`LraTableau::model_filtered_unit_equalities` result before integer completion.
It retains fixed-value equalities and the paper's one-row and same-sign unit
connector two-row shapes. General `EqRows` and non-unit connector equalities
remain outside this producer. The model-guided and complete-arrangement
fallbacks still handle compatibility; optional discovery is incomplete.

The complete online route repairs an owned tableau snapshot before applying
the current-value filter. Bounds-only rollback need not restore the previous
assignment, so callback history alone cannot establish feasibility. If no
batch is delivered, integer completion receives that same snapshot by move.
The model-guided/rebuilt checks already repair their full tableau copy. Proof
antecedents remain explicit SAT literals, with inactive and synthetic reasons
rejected before delivery. No private integer cut or branch can supply a proof.

Exact model grouping and full row traversal remain. The adaptation avoids
constructing and hashing numeric row offsets, but it is neither incremental
nor allocation-free. Discovery and snapshot repair have separate timers.

## Fresh correctness gates

The qualified Release snapshot is `44eee3c1`; production C++ is unchanged
since `183dce9a`. The
[qualification record](../../benchmarks/uflia-model-unit-20261111/qualification.json)
contains binary identities, exact profiles, commands, limits and SLURM jobs.
All solver builds, tests, fuzzing, timings and model validation ran on SLURM,
using the preparation helper. No reference timing runs were repeated.

- `make check`: all 47 C++ groups and 329 Python tests pass. The integration
  tests cover both producers, disabled export, delivery policies, pre-completion
  propagation, model coincidences, non-unit compatibility search, online/rebuild
  routes and printed models. Existing tableau tests pin unit-connector signs,
  large fixed offsets, deterministic spanning and bound rollback.
- ASan: all 47 C++ groups pass, followed by 300 QF_UFLIA differential checks
  (100 generated trials, 74 content-distinct inputs, repeated under `all`,
  `fixed` and `names`).
- Fresh generation: 8,000 matching answers against Z3, 1,000 for each of eight
  profiles: `all`, rebuilt, complete arrangement, `fixed`, `names`, `scan`,
  disabled export and strong preprocessing. These are 8,000 generated trials
  with 5,320 content-distinct inputs across the eight profiles. All 4,070 successful primary SAT
  rows pass separate direct Dolmen validation. There are no timeout, wrong-answer,
  crash, unsupported, parser/type-error, invalid-model or reference-disagreement
  rows in this gate.
- Real-seed mutations: 160 retained mutants from four SAT seeds, checked under
  both `all` and `fixed`. Each policy gives 157 SAT and three UNSAT matches;
  all 157 SAT rows and all four original seed models pass Dolmen. These are
  160 retained mutant files with 141 content-distinct inputs, checked twice;
  they are not 320 distinct mutants.
- CLI checks cover both producers and all five delivery settings; invalid
  `adaptive` and unknown selector values are rejected.
- `make lint` passes. Whole-tree clang-tidy/analyzer/include-cleaner covers all
  135 tracked C++ translation units. The
  [static review](../../benchmarks/uflia-model-unit-20261111/static-review.json)
  records 2,326 advisory warnings versus 2,292 in the accepted repair baseline:
  the new enum-width recommendations, changed options-padding message and two
  intentional negative enum tests explain the difference. The 407 include-cleaner
  occurrences are unchanged. Existing leak reports remain; no warning-free-tree
  claim is made.

Two earlier unit runs failed test assertions about fixture activation and the
scope of a snapshot counter. The fixtures were corrected before the final
fresh gates; neither failure was a wrong solver answer. The run ledger retains
both attempts.

## Diagnostic timings at 120 seconds

The frozen baseline is the accepted preprocessing-repair binary `5d7d4fe0`,
with production source equivalent to `6c8d1278`. Twelve selected QF_UFLIA
diagnostics run under six arms, each with three rotated repetitions: 216 native
timing rows. All use the default arithmetic settings, a 120-second timeout,
one requested physical CPU and an 8 GiB address-space cap inside an 8 GiB SLURM
allocation. Every one of the 114 successful SAT timing rows passes separate
Dolmen replay, including baseline rows and repeated runs. Model-validation time
does not enter the timing results. Decisive answers agree with archived cvc5
rows on SHA-identical inputs.

| Producer / delivery | Solved cases per repetition | Solved timing rows | PAR2 sum, 36 rows (s) |
| --- | ---: | ---: | ---: |
| Frozen baseline / off | 9/12 | 27/36 | 2,496.64 |
| Candidate legacy / off | 9/12 | 27/36 | 2,497.97 |
| Legacy / all | 10/12 | 30/36 | 2,066.81 |
| Model-unit / all | 11/12 | 33/36 | 1,043.90 |
| Legacy / fixed | 10/12 | 30/36 | 1,806.63 |
| Model-unit / fixed | 9/12 | 27/36 | 2,376.37 |

PAR2 assigns 240 seconds to each unsolved row. These are selected diagnostics,
not a full-corpus estimate. The disabled candidate has no gains or losses and
matches all pre-existing non-time counters on every common successful row.
Its common-pair geometric runtime ratio is 0.997 for pairs taking at least
50 ms; this is no evidence of a meaningful default-path change.

With `all` delivery, model-unit recovers `wisas/xs_37_57.smt2` in every repeat
and loses no solve against legacy/all. On common successes of at least 50 ms,
the geometric runtime ratio is 0.959. The aggregate hides regressions:

| Case | Delivery | Legacy median (s) | Model-unit median (s) |
| --- | --- | ---: | ---: |
| MathSAT `medium13` | all | 0.980 | 1.174 |
| MathSAT Wisa `xs-09-06-1-5-3-2` | all | 0.187 | 7.308 |
| Certora `39657…_48` | fixed | 47.627 | timeout in all three |
| Wisas `xs_19_29` | all | 24.598 | 2.209 |
| Wisas `xs_29_49` | all | 100.359 | 15.832 |

Three further rotated pairs confirm each regression: `medium13` takes
0.964 versus 1.146 seconds, the MathSAT Wisa case 0.186 versus 7.269 seconds,
and Certora `_48` 46.588 seconds versus three timeouts. All nine successful
SAT rows in this repeat pass separate Dolmen checks. Search changes dominate
some rows: for the MathSAT Wisa case, one representative
`all` run grows SAT decisions from 11,346 to 187,396 while fresh pairs fall
from 132 to 85. Discovery time grows from about 2.8 to 3.9 ms, far too little
to explain the seven-second runtime change. Missing/different proofs and their
spanning order can change the subsequent search. This comparison does not
isolate discovery cost while holding all selected equalities fixed.

## Mechanism evidence and decision

The [timing summary](../../benchmarks/uflia-model-unit-20261111/timing-summary.json)
records paired runtimes and per-row mechanism counters. Model-unit selection
increments `:lia.equality.legacy-scans-skipped`; no legacy discovery call runs
for that query. In representative `fixed` runs, Hash has 237 skipped calls,
and Certora `38347…_58` has 160. Both retain the legacy arm's SAT decisions,
LRA pivots and fresh-pair counts. Across the three repeats, their median
measured discovery times fall respectively from 71.9 to 42.9 ms and from
137.8 to 57.4 ms. These examples demonstrate a smaller discovery cost with
unchanged recorded search, not an overall solver speedup.

The feature is accepted as a correctness-qualified, selectable experiment.
Promoting either delivery policy or this producer to default is rejected on
this evidence. The `fixed` result is specifically worse on the selected
Certora case; `all` has both gains and regressions. No universal improvement,
full-corpus result, M124 result or closure of the cvc5 gap is claimed. Existing
uniform cactus plots remain historical and unchanged.

A concrete next experiment is to record each producer's explained equality
forest on the MathSAT Wisa regression, then compare bridge creation and SAT
search under controlled delivery. A change to scheduling, bridge activation,
proof selection or incremental model grouping needs its own design and fresh
gates. This task changes discovery selection only.
