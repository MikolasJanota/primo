# QF_LIA default profile update

Status: QF_LIA-only default update qualified on the scope below. The owner requested
QF_LIA's defaults become **M124 + uniform cubes + hybrid branching**. Other
logics keep their existing defaults. This changes configuration of existing
algorithms; it introduces no new integer inference or termination claim.

## Implementation checklist

- `default_lia_settings(LiaRoute::QfLia)` remains the single route-policy owner.
  Enable hybrid and uniform cubes; disable explicit integer-slack obligations;
  retain integer-bound rounding, primitive rows and integral patch moves.
- Keep the generic `LiaSettings{}` baseline and QF_UFLIA policy unchanged.
  Budgets, preprocessing, row propagation, equality export and EUF options
  retain their values. Explicit route-supported overrides still win.
- Settings remain copied by value into solver steps/components. SAT-bound
  registration, history, resource accounting and private cut lifetimes retain
  their existing contracts.
- Update CLI defaults and strategy identity tests. Pin old engine settings in
  stage-specific tests, and test actual cube/hybrid execution under new defaults.
- Build through the SLURM preparation helper; run native/ASan checks, fresh
  campaign-grade QF_LIA generation, real-seed mutations, tracked/scoped models
  and controls. Every successful selected SAT row requires Dolmen acceptance.
- Compare the exact new default against old settings and the individual
  cube/hybrid additions on the existing frozen LIA selection. Keep timings
  separate from model validation; retain incomplete outcomes and regressions.
- Update current documentation, distinguish historical evaluations, obtain
  independent documentation/evidence review, build/render/check docs and commit.

## Basis and scope

The existing {ref}`cube implementation <lia-cubes>` follows
Bjørner–Nachmanson, CAV 2024 §4.2, with its documented subset and candidate
validation. {ref}`Hybrid branching <lia-hybrid-branching>`
implements ordinary-coordinate splitting from the documented partial Griggio
§5 policy. Proof-derived combination splits remain absent. The earlier
[cube](lia-cubes.md), [hybrid](lia-hybrid-20261111.md) and
[combination](arithmetic-combinations.md) studies motivate this configuration;
they do not measure this exact combined default on the current source.

M124 omits redundant integer-slack obligations from patching/completion. Proved
slack integrality can still justify preparation-time threshold rounding. All
actual Int terms must still be integral before accepting a model.

## Effective settings and overrides

The {qfliadefaultsource}`route policy <src/smt/lia_settings_policy.cpp>` changes
three QF_LIA fields: `hybrid_branching=true`, `cube_mode=Uniform` and
`integer_slacks=false`. The policy returns an owning `LiaSettings` value;
solver steps and arithmetic components retain their existing by-value copies.
The generic settings object remains unchanged, so QF_UFLIA continues with
private branching and no cube attempt. Other logic routes are untouched.

Uniform cubes run after patching/GCD and before cuts. If that sufficient model
attempt fails, completion continues. Hybrid branching then permits at most
`min(100000, N)` private split nodes, where `N` counts original integer terms,
before requesting a SAT decision. This changes search order and can regress
individual inputs; ordinary coordinate splits still have no general termination
claim. The default factor `1` is an engineering choice, not a tuned universal
optimum. Cut and patch budgets remain `100`; the cube repair budget stays
`1000`; the fresh split-atom cap stays `0` (unlimited).

Explicit options still override the selected route. To recover the previous
QF_LIA profile, add:

```console
--lia-branching=private --lia-cube=off --lia-integer-slacks
```

`--lia-cube=off` alone disables cubes; `--lia-branching=private` alone disables
the SAT handoff. The {qfliadefaultsource}`policy and strategy tests
<tests/unit/tactic_tests.cpp#L1243-L1437>` check both routes, opposite-valued
overrides and the canonical strategy identity. The
{qfliadefaultsource}`QF_LIA regression
<tests/unit/qf_lia_solver_tests.cpp#L1057-L1090>` checks actual cube adoption and
SAT split generation under route defaults, and resource exhaustion when both
shortcuts are explicitly disabled. Stage-specific tests pin their former
engine baseline instead of inheriting the new policy.

## Fresh correctness gate

Source `b3218ec9` and native SHA-256
`b7fdf6ad1df4f64a7b84fa1dffd1e18f11edf8868913952c036dc780a2183ae1`
passed SLURM `make check` (49 native groups, 400 Python tests), 49 ASan groups
and 12 documentation-example/help checks. The
{download}`qualification record <../../benchmarks/qflia-defaults-20261119/qualification.json>`
contains the exact profiles, seeds, commands, receipts, hashes and model metadata.

Seven 1,000-input generated tasks compare against Z3: three run the actual new
QF_LIA default, three run QF_LIA controls, and one runs unchanged QF_UFLIA.
There are **6,912 passes, 87 timeouts and one reference-only timeout**. The actual
new-default tasks contain 3,000 cases: 2,952 passes and 48 timeouts. The real-seed
campaign adds **2,520/2,520 passing mutants** from twelve family-balanced seeds
per task, with separate checks of those original seeds. All 65 tracked
regression measurements pass, including scoped sessions. The 88 saved generated
resource/reference-timeout inputs match the classified summary rows and content
hashes; mutation bug directories are empty.

Every selected native SAT model/query passes direct Dolmen validation:
**7,412 ordinary models plus 40 scoped SAT queries = 7,452 checks**. These are
measurement/query counts, not distinct benchmark counts. Z3 is version 5.1.0;
Dolmen is 0.10. Generated answer budgets are 5 seconds per native/reference run;
ordinary generated/mutant model budgets are 30 seconds each for native replay
and Dolmen. Timeouts remain incomplete outcomes. There are no wrong answers,
crashes, invalid models or unexplained disagreements in this scope. These
checks are separate from the timing comparison below.

## Comparison protocol

The frozen {download}`timing card <../../benchmarks/qflia-defaults-20261119/timing.json>`
retains the established 200-case QF_LIA selection and every input content hash.
All four profiles use the same candidate binary, 120-second native limits and
an 8 GiB address-space cap. Each allocated core runs its four measurements
sequentially; the order rotates across inputs.

| Label | Integrality settings | Cubes | Branching |
| --- | --- | --- | --- |
| `OLD` | Former four-switch default | Off | Private |
| `HYBRID` | M124 | Off | Hybrid |
| `CUBE` | M124 | Uniform | Private |
| `NEW` | M124 | Uniform | Hybrid |

`OLD` reproduces the previous settings on this binary, isolating the requested
configuration change. `NEW` versus `HYBRID` measures adding cubes with hybrid
already enabled; `NEW` versus `CUBE` measures adding hybrid with cubes already
enabled. This is one rotated pass on a targeted selection, not a full-corpus
joint-profile evaluation or global parameter tuning. Reference solvers are not
rerun for speed. Every successful native SAT timing row requires a separate
Dolmen replay after the timing stage; model-checking time is excluded.


## Results and retained regressions

The {download}`audited comparison <../../benchmarks/qflia-defaults-20261119/comparison/summary.json>`
and {download}`832-row reproduction bundle <../../benchmarks/qflia-defaults-20261119/README.md>`
separate the 800 primary measurements from 32 diagnostic repeat measurements.
All **332 successful SAT timing rows** pass separate direct Dolmen validation;
the four UNSAT-loss repeat inputs add no SAT rows. Including the correctness
gates, there are **7,784 direct Dolmen model/query checks**.

| Profile | Solved / 200 | SAT | UNSAT | Mean PAR2 (s) | Median RSS (MiB) |
| --- | ---: | ---: | ---: | ---: | ---: |
| `OLD` | 104 | 55 | 49 | 120.62 | 19.4 |
| `HYBRID` | 150 | 89 | 61 | 65.47 | 15.9 |
| `CUBE` | 158 | 90 | 68 | 55.40 | 15.9 |
| `NEW` | 159 | 98 | 61 | 54.59 | 15.9 |

`NEW` gains 56 solves and loses one relative to `OLD`, lowering mean PAR2 by
54.7% on this selection. Among the 103 common solves, the conditional geometric
mean runtime ratio is 0.542 on the 48 cases where `OLD` took at least one second.
That is a one-pass observation, not a repeat-qualified runtime claim.

Relative to `HYBRID`, adding cubes gains nine solves with no lost solve. Relative
to `CUBE`, adding hybrid gains eleven and loses ten: the net one-solve gain and
1.47% lower mean PAR2 are a near tie with substantial solved-set churn. This
comparison does not establish that the combined profile is globally better
than cubes with private branching. It records the tradeoff of the requested
combined default; global tuning remains future work.

The old-default loss is `cut_lemmas/15-vars/cut_lemma_01_005.smt2`. Two further
rotated paired repetitions cover that input, `cut_lemmas/15-vars/cut_lemma_01_001.smt2`
and `prime-cone/prime_cone_unsat_10.smt2` / `prime_cone_unsat_11.smt2`.
`CUBE` closes all four in under 0.3 seconds each in both repetitions; `NEW` and
`HYBRID` time out at 120 seconds on all four. `OLD` closes its one lost input in
about 9.6 seconds in both repeats. These losses reproduce: the private branching profile closes these cases much
faster than the hybrid profiles. The other six
`CUBE`-relative losses remain single-pass observations; the full gain/loss lists
and family summaries are in the audited JSON. The repeat rows are not pooled
into the 200-case table.

| Profile | Normal unknown | Timeout | Handled memory exhaustion | Peak RSS (GiB) |
| --- | ---: | ---: | ---: | ---: |
| `OLD` | 63 | 31 | 2 | 7.41 |
| `HYBRID` | 0 | 49 | 1 | 7.39 |
| `CUBE` | 20 | 20 | 2 | 7.39 |
| `NEW` | 0 | 40 | 1 | 7.38 |

The normal unknowns report the private node limit. All six memory exits report
`std::bad_alloc` with exit status 2 under the 8 GiB address-space cap. They are
unsolved resource outcomes, not crashes or valid answers. PAR2 charges every
unsolved outcome twice the 120-second limit.

Successful `NEW` rows publish 20,981 private nodes, 360 fresh split atoms/SAT
resumes, 36 adopted cube models and a maximum of 191 generated atoms in one
solve. All 360 registrations use existing arithmetic coordinates. `OLD` and
`CUBE` publish 287,858 and 266,624 private nodes respectively; neither exports
hybrid splits. The instrumented `NEW` private-completion and resumed-SAT buckets
are 139.198 and 0.751 seconds. These counters cover different successful-row
sets and omit unfinished tails; they show mechanism use, not a profile-wide
cost decomposition. The machine-readable report retains the other conflict,
cut, pivot, availability and per-family counters.

## Static, scheduling and audit provenance

Both whole-tree scans cover all 141 tracked non-generated C++ translation units,
with no missing/changed inputs, tool failures or unused-include failures.
They record 882 tidy and 409 include-cleaner located diagnostics (744 and 401
normalized file/message/check signatures). Two added findings were missing
direct cube-mode includes in the policy and tactic test. Both are fixed;
SLURM supplement `434542` recompiles both Release objects byte-for-byte
identically to the qualified objects. Focused analysis finds no new signature;
one existing header-padding advisory remains. Existing whole-tree advisories
are retained, so this is not a warning-free claim.

Timing runs on nodes 02, 03 and 04. The {download}`recorded CPU topology
<../../benchmarks/qflia-defaults-20261119/topology.json>` proves every captured
two-CPU affinity mask contains the SMT siblings of one physical core. All four
profiles remain sequential within that allocation. Each model array depends
on all three complete timing arrays, preserving the global timing/model barrier.

The runner uses the input's declared SMT-LIB status when the frozen reference
label is unknown. One retained {download}`content-matched input
<../../benchmarks/qflia-defaults-20261119/status-inputs/QF_LIA/cut_lemmas/20-vars/cut_lemma_01_001.smt2>`
reproduces that fallback: its declared status is UNSAT. Supplemental Z3,
Z3 arithmetic-engine-2 and cvc5 checks each time out at 120 seconds with no
contrary answer. They supply no new answer or speed baseline. The final auditor
checks the same hash-matched declared-status fallback as the native runner.

Qualification jobs are `434382`, `434385`–`434387`; full static is `434384`.
Primary timing is `434409`, loss repetitions are `434612`/`434616`, and separate
models are `434620`/`434723`/`434824`. Topology is `434828`; the qualification
record retains supplementary diagnostic attempts and exact hashes. Source
`b3218ec9` is an ancestor of the branch; the only subsequent C++ changes are the
two object-identical direct includes. Eleven focused report tests cover outcome
classification, decimal-string time input, nonfinite rejection, physical-core
identity and declared-status/hash checks.
