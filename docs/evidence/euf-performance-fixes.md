# EUF mechanical fixes and demand-atom evaluation

Status: repaired source `6e53a710` passes native, ASan and fresh correctness
gates. The integrated gate checks 21,000 generated cases, 6,826 real-seed
mutations and 318 regression measurements; 19,125 separate SAT models pass
Dolmen. All 5,862 native timing rows and 2,753 separate SAT-model replays pass the
full audit. Mechanical changes give a modest targeted UF gain; demand atoms
regress. Additional paired regression repetitions confirm the demand regressions.
Defaults stay unchanged.

The [implementation checklist](../leads/euf-performance-fixes.md) and
[EUF representation](../implementation/euf.md#conflict-selected-ackermann-prototype)
map the changes to their owners and limitations. Dutertre and de Moura's
conflict-selected congruence clause {cite:p}`dutertre2006yices` is the algorithmic
basis; the caps, queue lifetimes, negative cache and safe-boundary scheduling
are Primo engineering choices. The [earlier pilot](conflict-ackermann.md)
measured the older existing-atom policy, not the revised implementation.

## Frozen comparison

| Label | Native source | Configuration and purpose |
| --- | --- | --- |
| BASE | `13230b6d` | Frozen pre-change production binary; source behavior agrees with pre-change HEAD `690ddc7f`. Default options, with statistics. |
| MECH | `66ebec78` | First mechanical/typed-statistics milestone; default options. |
| OFF | `6e53a710` | Integrated implementation; conflict Ackermann remains disabled. |
| OLDON | `13230b6d` | Frozen original conflict-Ackermann emission policy. |
| ON | `6e53a710` | Revised selection, using existing equality atoms. |
| DEMAND | `6e53a710` | Revised emission plus demand atoms, default fresh-equality limit 4096. |

All timing commands use `--quiet --stats`. ON and OLDON add
`--euf-conflict-ackermann`; DEMAND also adds
`--euf-conflict-ackermann-atoms=demand`. Complete native hashes, ordered arguments
and input hashes belong to the run cards and immutable receipts. The
{download}`card generator <../../scripts/qualification/euf_performance/configure.py>`
and {download}`auditor <../../scripts/qualification/euf_performance/audit.py>`
are reproducibility tools, not solver implementations.

The frozen 80-case pilot has 40 QF_UF and 40 QF_UFLIA cases, enriched for the
previous exploration bottleneck. It runs all six profiles, in three rotated and
reversed repetitions: 1440 native rows. The broader control set contains the
existing 697 arithmetic inputs plus the 40 UF pilot inputs: 200 QF_LIA,
300 QF_UFLIA, 100 QF_LRA, 97 QF_UFLRA and 40 QF_UF. BASE/OFF are paired three
times on these 737 cases: 4422 rows. The three previously quarantined arithmetic
inputs remain excluded, as recorded in the frozen parent selection. UF controls
are the enriched pilot subset; this is not a uniform full-corpus ranking.

Each measurement has a 30-second wall limit, an 8 GiB native address-space cap,
and one requested CPU per case/repetition SLURM allocation. Profiles run
sequentially within that allocation; recorded process affinity contains two
logical CPUs. Timeouts, resource returns and unsupported results count as
unsolved in PAR2, even if an answer was printed during termination. RSS is the
fresh native child's high-water mark. Solved counts, paired runtimes, PAR2,
RSS and mechanism counters are reported by logic, with stable gains/losses and
repeated regressions separated from short-run noise.

Every successful native SAT timing row must pass a separately scheduled replay
with original assertions, model production, `--check-model` and direct Dolmen
validation. Replay time does not enter the speed rows. Repetitions and option
profiles remain distinct validation targets. A timing shard without complete
model coverage remains provisional.

## Correctness and mechanism stages

The first milestone's {download}`audited gate <../../benchmarks/euf-performance-20261118/mechanical-qualification.json>`
records 9000 generated and 2855 mutated inputs, with 20 timeouts and 12 incomplete
mutations, respectively; 8617 SAT models passed separate Dolmen checks. Those
counts qualify `66ebec78` only.

The integrated gate uses 21 exact logic/option profiles. It includes all five
ground-logic defaults; existing-atom OFF/diagnostic/emitting variants; demand,
diagnostic-only demand and a one-atom limit in UF/UFLIA; both supported alternate
explanation searches; and mixed demand with the model-unit equality producer or
immediate hybrid integer handoff. Each generated profile gets 1000 fresh cases against Z3: FuzzSMT for UF/LIA,
`qf_uflia_fuzz.py` for UFLIA, `qf_lra_boolean_arithmetic_fuzz.py` for LRA, and
`qf_uflra_smoke_fuzz.py` for UFLRA. Real-seed mutations, tracked regressions, scoped
sessions and all successful SAT models are checked separately. The native and
ASan gate is {download}`a pinned SLURM driver <../../scripts/qualification/euf_performance/verify.sbatch>`;
its ASan revision is explicitly set because the remote source archive has no
Git checkout.

A focused nested mixed probe on preliminary source `d1f71fb6` selected two
congruence instances, created one equality atom used by both instances, and resumed SAT
once. A direct variant already had the argument atom and used existing-atom
clauses. These are mechanism diagnostics, not timing evidence or qualification
of later source. The matching native regression requires demand growth for a
non-interface equality, while leaving the arithmetic owner with its one input
atom and no arrangement pairs.

The local GCC ABI size diagnostic found `Statistics` objects of 2424 bytes
before the migration, 7176 bytes at the mechanical milestone, and 7848 bytes
with the integrated registry. This was a compile-to-assembly size check, not a
runtime measurement. Fixed-array growth and removed string/map work must be
assessed together with measured RSS and runtimes. Closure checkpoints use their
separate compact statistics structure.

The preliminary full-tree {download}`static inventory <../../benchmarks/euf-performance-20261118/static-preliminary.json>`
completed both scans over 141 C++ translation units, with no missing/changed
inputs or tool errors and no blocking unused includes. It records advisory
findings and their inspected scope; it is not a claim that the whole tree is
warning-free. The `abb4d19c` integrated scan also completed 141/141 units with zero tool
errors. The repaired-source {download}`final scan <../../benchmarks/euf-performance-20261118/static-final.json>`
also covers 141/141 units in both tools with zero tool errors, no new normalized
diagnostic messages and no blocking unused includes. The 880 tidy and 407
include-cleaner advisories remain; the source is not warning-free.

## Model-fact failure and repair

Source `abb4d19c` passed 21 regression and 21 generated-input tasks, but the
real-seed mutation array passed only 20 of 21 tasks. The model-unit mixed profile
had 241 SAT replays: 238 passed Dolmen and three exited with
`model: inconsistent final component equalities`. The original SAT answers
agreed with Z3. Isolation job 414923 reproduces all three errors with demand
and the default or legacy equality producer too. One case validates with both
the frozen baseline and current existing-atom mode; the other two time out
without demand at 30 seconds.

The diagnostic build found a pure EUF contradiction in exported facts: an
optional positive-only equality's false assignment had been filtered online
and subsequently exported as a required disequality. The full checker already
enforces all assignments; the complete-arrangement online path must do so too.

Repaired source `6e53a710` disables filtering in the mixed component's owned
online options, matching its full-check/export contract. Standalone filtering
is unchanged. The reduced {download}`regression <../../benchmarks/regressions/qf_uflia_demand_filtered_model_fact.smt2>`
requires fresh demand atoms, complete fallback and a checked SAT model, under
both automatic and explicit-on filtering requests. All four inputs pass five
independent profiles in {download}`focused replays <../../benchmarks/euf-performance-20261118/model-fact-repair.json>`
(job 414946): default, model-unit, explicit on/off, and complete arrangement.
The original failure artifacts are preserved. The repaired source passes its
{download}`fresh integrated gate <../../benchmarks/euf-performance-20261118/integrated-qualification.json>`:
20,986 generated answers match Z3, two generated runs return a confirmed integer
branch-node resource limit, and 12 time out. Of 6,826 mutated inputs, 6,795 match
and 31 time out; all original seed checks pass. There are no unresolved native
unknowns in mutations, wrong answers, crashes or invalid models. The separate
SAT-model total is 19,125, with the 20 focused repair replays recorded separately.
All 12 documentation examples/help checks pass. Native timing arrays
415008–415010 and separate SAT-model arrays 417467–417469 completed. All 5,862
native rows and all 2,753 successful SAT measurements pass the full audit.


## Timing findings

The six-profile pilot preserves the solved count for the mechanical fixes and
existing-atom emission. On 40 UF inputs, BASE/MECH/OFF/ON each solve 34 in every
repetition; DEMAND solves 31. On 40 UFLIA inputs the corresponding counts are
28 and 27. The pooled mean PAR2 values are:

| Logic | BASE | MECH | OFF | OLDON | ON | DEMAND |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| QF_UF | 11.060 | 10.930 | 10.921 | 11.140 | 10.972 | 17.470 |
| QF_UFLIA | 21.148 | 21.132 | 21.100 | 21.148 | 21.141 | 22.764 |

The mechanical UF effect is clearer on the 15 common-solved
cases whose BASE median is at least one second: the geometric mean of each
case's median paired MECH/BASE ratio is 0.926. OFF/BASE is 0.925 on the same
subset. No extra solves result. On the 12 eligible UFLIA cases MECH/BASE is
0.993: this provides little evidence of a useful UFLIA improvement. These are
conditional ratios on an enriched selection, without a significance estimate,
not a full-corpus speedup.

Revised existing-atom emission is also not an overall win over current OFF:
ON/OFF is 1.024 on 13 eligible UF cases and 1.008 on 12 UFLIA cases. Demand is
substantially worse: four stable lost solves, and ON-to-DEMAND ratios of 3.220
on ten common-solved UF cases and 1.448 on eleven UFLIA cases. The 25 distinct
stable-loss or repeated-slowdown inputs were selected for three additional
paired OFF/ON/DEMAND repetitions. Selection includes every stable loss and every
common-solved slowdown of at least 10% and 0.5 seconds in each initial
repetition; it is a regression follow-up, not a new unbiased performance sample.

The five-logic default controls have unchanged solved counts between BASE and
OFF in each matched repetition. Pooled PAR2 changes are small: LIA
34.103 to 34.100 seconds, LRA 13.852 to 13.847, UFLIA 10.861 to 10.853 and UFLRA
8.039 to 8.043. UF controls are the same enriched 40 inputs measured again;
their PAR2 changes from 11.057 to 10.913. These arithmetic differences do not
establish a performance benefit. There are no stable lost solves or cases
meeting the repeated slowdown threshold in the default controls.

The demand path is active, rather than silently falling back to existing atoms.
In `solve_qf_uf_engine_impl`, a growth handoff destroys the online propagator and
prepared flat theory, installs pending equalities, reconstructs both owners and
re-registers observed SAT variables. Among successful UF measurements, 63 rows explicitly print demand activity:
124,533 fresh atoms and 29,340 SAT resumes in total, with a peak of 4,096 fresh
atoms in one query. The corresponding 69 reporting UFLIA rows total 20,031 atoms
and 9,189 resumes, with a peak of 1,196. These totals span three repetitions;
unprinted values remain unavailable, and incomplete measurements are summarized
separately. The counters are consistent with repeated rebuild cost and extra
Boolean choices.
This is an implementation-based explanation to investigate; the experiment
does not isolate rebuild time from the changed search. It does not demonstrate
Yices-style performance, and no reference-solver timing ranking is added here.
Z3 is the correctness oracle in the fresh gates.

Conflict Ackermann emission remains opt-in and equality atoms default to
`existing`. Demand is retained as a bounded experimental prototype, without a
performance recommendation or default promotion. Batching growth and reducing
rebuild work require a separate correctness and performance evaluation.

## Additional paired regression repetitions

All 25 selected cases completed three additional rotated OFF/ON/DEMAND
repetitions: 225 native measurements, followed by 50 successful SAT measurements
passing separate direct Dolmen checks. Native jobs 419920–419922 and model jobs
419995, 419996 and 420021 completed successfully. The four lost solves recur in
all three additional repetitions. Of the 21 original repeated-slowdown cases,
20 again meet the 10% and 0.5-second threshold in every repetition against both
OFF and ON. The remaining original slowdown case, Certora `39657`, has a
DEMAND timeout in repetition zero and takes 28.7–29.6 seconds in the other
two, against OFF at 24.8–25.4 seconds; it is excluded from the common-solved
slowdown calculation. No case gains a stable solve. This confirms a substantial demand
regression on the selected problems; it is not machine noise.

On the 14 eligible common-solved cases in this regression sample, the geometric
mean paired ratio is 3.169 for DEMAND/OFF and 3.075 for DEMAND/ON. These
selection-conditioned ratios are diagnostic and must not be extrapolated to a
corpus-wide slowdown. The {download}`follow-up comparison <../../benchmarks/euf-performance-20261118/evaluation/followups.json>`,
the {download}`225 native measurements <../../benchmarks/euf-performance-20261118/evaluation/followup-native-rows.csv>`
and {download}`model receipts <../../benchmarks/euf-performance-20261118/evaluation/followup-receipts.json>`
preserve all selected cases and results. The main tables/plots below retain the
original repetitions; follow-up timings are not pooled into them.

## Audited measurements and cactus plots

Download the {download}`native CSV <../../benchmarks/euf-performance-20261118/evaluation/native-rows.csv>`,
{download}`paired comparisons <../../benchmarks/euf-performance-20261118/evaluation/paired-analysis.json>`,
{download}`rollup <../../benchmarks/euf-performance-20261118/evaluation/rollup.json>`,
{download}`model/audit receipts <../../benchmarks/euf-performance-20261118/evaluation/audit-receipts.json>`
and {download}`export provenance <../../benchmarks/euf-performance-20261118/evaluation/provenance.json>`.

```{figure} ../_static/euf-performance-20261118/pilot-cactus.svg
:alt: Median runtime cactus plots for the six profiles on UF and UFLIA pilot inputs.

Pilot: median of three runs, with at least two successful repetitions required.
```

```{figure} ../_static/euf-performance-20261118/controls-cactus.svg
:alt: BASE and OFF median runtime cactus plots for all five ground logics.

Default controls: BASE and OFF nearly overlap. UF is the enriched pilot subset.
```

| Cohort | Logic | Profile | Solved (three repetitions) | Mean PAR2 (s) | Median / max RSS (MiB) |
| --- | --- | --- | --- | ---: | ---: |
| pilot | QF_UF | BASE | 34/34/34 / 40 | 11.060 | 20.2 / 189.6 |
| pilot | QF_UF | MECH | 34/34/34 / 40 | 10.930 | 19.8 / 194.1 |
| pilot | QF_UF | OFF | 34/34/34 / 40 | 10.921 | 20.0 / 191.3 |
| pilot | QF_UF | OLDON | 34/34/34 / 40 | 11.140 | 20.3 / 193.4 |
| pilot | QF_UF | ON | 34/34/34 / 40 | 10.972 | 20.3 / 193.4 |
| pilot | QF_UF | DEMAND | 31/31/31 / 40 | 17.470 | 30.1 / 208.3 |
| pilot | QF_UFLIA | BASE | 28/28/28 / 40 | 21.148 | 15.7 / 672.8 |
| pilot | QF_UFLIA | MECH | 28/28/28 / 40 | 21.132 | 15.7 / 675.1 |
| pilot | QF_UFLIA | OFF | 28/28/28 / 40 | 21.100 | 15.7 / 669.6 |
| pilot | QF_UFLIA | OLDON | 28/28/28 / 40 | 21.148 | 15.7 / 676.7 |
| pilot | QF_UFLIA | ON | 28/28/28 / 40 | 21.141 | 15.7 / 675.0 |
| pilot | QF_UFLIA | DEMAND | 27/27/27 / 40 | 22.764 | 18.2 / 686.3 |
| controls | QF_LIA | BASE | 91/90/90 / 200 | 34.103 | 17.2 / 4352.3 |
| controls | QF_LIA | OFF | 91/90/90 / 200 | 34.100 | 17.3 / 4355.4 |
| controls | QF_LRA | BASE | 80/80/80 / 100 | 13.852 | 18.4 / 1509.4 |
| controls | QF_LRA | OFF | 80/80/80 / 100 | 13.847 | 18.8 / 1505.9 |
| controls | QF_UF | BASE | 34/34/34 / 40 | 11.057 | 19.9 / 192.6 |
| controls | QF_UF | OFF | 34/34/34 / 40 | 10.913 | 20.1 / 194.7 |
| controls | QF_UFLIA | BASE | 255/255/255 / 300 | 10.861 | 11.8 / 1035.2 |
| controls | QF_UFLIA | OFF | 255/255/255 / 300 | 10.853 | 12.2 / 1046.2 |
| controls | QF_UFLRA | BASE | 88/88/88 / 97 | 8.039 | 21.5 / 5047.6 |
| controls | QF_UFLRA | OFF | 88/88/88 / 97 | 8.043 | 21.4 / 5048.3 |


Demand also increases native memory: pilot UF median RSS increases from 20.0 MiB OFF to 30.1 MiB DEMAND, with maxima 191.3 and 208.3 MiB.
UFLIA increases from 15.7 to 18.2 MiB, with maxima 669.6 and 686.3 MiB. These are
pooled native measurements across all outcomes and repetitions, separate from
model replay memory. The full table preserves the other profiles and controls.

## Reproducing the report

Run-card generation and native/model execution use the frozen selections and
source hashes above. Private corpus roots, binaries and cluster routes remain
outside the repository. After collecting the complete metadata and separate
model receipts, run:

```sh
python3 scripts/qualification/euf_performance/audit.py \
  --configs "$CARDS" --rows "$ROWS" --out "$AUDIT"
python3 scripts/qualification/euf_performance/report.py \
  --configs "$CARDS" --rows "$ROWS" --analysis "$AUDIT" \
  --out "$EXPORT" --figures "$FIGURES"
python3 scripts/qualification/euf_performance/followups.py \
  --configs "$FOLLOWUP_CARDS" --rows "$FOLLOWUP_ROWS" --out "$FOLLOWUP_AUDIT"
```

The exporter requires all 5,862 audited native rows and their hash-bound model
receipts. CSV and tables use only Python's standard library; optional SVG
rendering uses Matplotlib in a separate plotting environment. The plots use the
median of three runtimes with unsolved repetitions censored at infinity: a case
appears only if at least two repetitions succeed. Tables show each repetition's
solved count, pooled mean PAR2 and median/maximum native RSS over all outcomes.
Only explicitly printed successful-row mechanism counters are totaled; missing
values remain unavailable, and peaks use a maximum. The full analysis retains
incomplete-row snapshots separately. Follow-up comparisons use the same paired
thresholds as the initial selection.
