(lia-hybrid-evidence)=
# Hybrid integer branching: qualification and tradeoffs

**Accepted as an opt-in policy. Private branching remains the default.**
On the requested 200-input QF_LIA selection, hybrid solves 134 inputs versus
104 for frozen pre-change HEAD. On the 300-input QF_UFLIA selection it solves
260 versus 259. The four Certora diagnostics show no solved-count improvement.
Five LIA regressions reproduce in two additional paired runs, including two
lost solves. These results justify retaining an experimental option, with no
claim that it improves every family or should become the default.

```console
primo --lia-branching=hybrid input.smt2
```

The {ref}`implementation <lia-hybrid-branching>` describes
ownership, exact history scores, bound restoration, generated atoms and
failed-assumption recovery. The [paper-grounded checklist](../leads/lia-hybrid-branching.md)
was written before implementation. This is the ordinary-coordinate subset of
Griggio §5 {cite:p}`Griggio12`, combined with SAT scheduling motivated by
splitting on demand {cite:p}`BarrettNOT06`. Griggio §5.1's proof-derived
combination splits are absent, and no new termination theorem is claimed.
The default private factor of one is a Primo engineering choice.

## Frozen comparison and resource limits

The implementation commit is `cd258f6a`; all its tracked `src/` files are
byte-identical to qualified snapshot `9e01197e`. The baseline is pre-change
HEAD `c49585a0`. The [qualification record](../../benchmarks/lia-hybrid-20261111/qualification.json)
pins full revisions, binary hashes, commands, jobs, limits and artifact hashes.
The solver/theory/SAT sources are unchanged since development snapshot
`736bf97a`; only CLI help changed before the final snapshot. Later evidence
and documentation-example repairs do not change the solver.

All solver builds, tests, fuzzing, timings, model validation and GDB sampling
ran on SLURM. Builds used `slurm_prepare_benchmark_workspace.py --build-via-slurm`.
Timings used Release binaries with CaDiCaL `cadical-3.0.0-7b99c07`, one requested
physical CPU, a 10 GiB SLURM allocation and an 8 GiB native address-space cap.
The 120-second harness allows termination cleanup, so killed rows can report
slightly over 120 seconds. PAR2 assigns **240 seconds to every unsolved row**;
the tables report its sum, not its mean. Model replay and profiling are outside
all speed measurements.

The [selection](../../benchmarks/lia-hybrid-20261111/selection.json) reuses the
existing 200-case LIA and 300-case SMT-COMP 2026 UFLIA selections, with exact
input hashes. Archived reference answers are reused only for those pinned
inputs; declared status is the fallback for unknown/error metadata. There is
no new reference-solver timing comparison. Both profiles use `--quiet --stats`;
hybrid adds `--lia-branching=hybrid` with factor one and unlimited fresh atoms.
Pair order rotates by slot. The Certora diagnostics additionally enable
`--qf-uflia-lia-equality-producer=model-unit --qf-uflia-tableau-equalities=all`
in both arms, with three rotated paired repetitions per input.

## Main results

| Selection / policy | Solved | SAT / UNSAT | Unknown | Timeout | Memory out | PAR2 sum (s) | Peak RSS (MiB) |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| LIA / private | 104/200 | 55 / 49 | 63 | 31 | 2 | 24,115.41 | 7,584.98 |
| LIA / hybrid | 134/200 | 83 / 51 | 0 | 65 | 1 | 17,037.37 | 7,587.62 |
| UFLIA / private | 259/300 | 203 / 56 | 0 | 41 | 0 | 10,615.58 | 2,869.75 |
| UFLIA / hybrid | 260/300 | 204 / 56 | 0 | 40 | 0 | 10,343.97 | 2,878.48 |
| Certora repetitions / private | 6/12 | 6 / 0 | 0 | 6 | 0 | 1,694.00 | 1,277.84 |
| Certora repetitions / hybrid | 6/12 | 6 / 0 | 0 | 6 | 0 | 1,694.18 | 1,280.82 |

LIA has 32 gains and two losses, a net gain of 30 and a **29.35% PAR2 reduction**.
Hybrid recovers 29 clean node-limit unknowns and three timeouts. Nineteen of
those gains use external splits; the other thirteen close during history-based
private search. This comparison measures the combined history/budget/SAT policy,
not an ablation isolating the benefit of SAT handoff.

UFLIA has one gain and no loss, a 2.56% PAR2 reduction. The recovered Certora-family
input `38347_092cc73601c78e45f4f9_58_QF_UFLIA.smt2` takes 52.47 seconds and two
generated bounds; it is distinct from the four diagnostic inputs. On those four
diagnostics, the same two inputs solve in every repetition and the other two
time out in every repetition. Their runtime change is effectively neutral.

On common solved pairs, hybrid/private median and geometric-mean runtime ratios
are respectively 0.9997 and 0.9877 for LIA (102 pairs), 1.0038 and 0.9282 for
UFLIA (259), and 0.9990 and 0.9992 for Certora repetitions (six). Tiny inputs
are included, so these ratios should be read alongside the absolute timings.
The [full summary](../../benchmarks/lia-hybrid-20261111/paired/summary.json)
contains every comparison and family. No wrong answer, signal crash,
unexplained error, unsupported row or paired disagreement occurred. The three
memory-out rows are diagnosed `std::bad_alloc` exits on Dartagnan stack inputs;
they remain unsolved in PAR2.

### Family distribution

The unchanged families remain in the machine-readable summary. Selected families
show where the coverage changes and where time worsens:

| Family | Inputs | Private solved | Hybrid solved | Private PAR2 (s) | Hybrid PAR2 (s) |
| --- | ---: | ---: | ---: | ---: | ---: |
| LIA `convert` | 12 | 5 | 12 | 1,684.70 | 4.75 |
| LIA `dillig` | 11 | 2 | 9 | 2,160.02 | 480.22 |
| LIA `CAV_2009_benchmarks` | 13 | 2 | 7 | 2,640.02 | 1,440.14 |
| LIA `20180326-Bromberger` | 17 | 3 | 7 | 3,360.03 | 2,400.22 |
| LIA `slacks` | 9 | 3 | 7 | 1,448.12 | 481.32 |
| LIA `prime-cone` | 11 | 2 | 5 | 2,160.02 | 1,535.83 |
| LIA `cut_lemmas` | 11 | 4 | 2 | 1,692.25 | 2,160.02 |
| LIA `tightrhombus` | 10 | 2 | 2 | 1,920.03 | 1,933.58 |
| UFLIA Certora family | 37 | 7 | 8 | 7,332.48 | 7,146.08 |
| UFLIA `mathsat` | 202 | 191 | 191 | 3,126.47 | 3,087.22 |
| UFLIA `wisas` | 61 | 61 | 61 | 156.62 | 110.67 |

### Repeated regressions and profiles

Every lost solve and every common solve with a slowdown above 1.2 times and
0.1 seconds received two additional rotated paired repetitions. All five
regressions reproduce. The following medians use the original plus both repeats;
original rows remain unchanged in headline totals.

| LIA case | Private median (s) | Hybrid median (s) | Observation |
| --- | ---: | ---: | --- |
| `pb2010/normalized-j3031_7-sat` | 4.380 | 20.815 | 294 private nodes versus two; no external split |
| `cut_lemmas/15-vars/cut_lemma_01_005` | 9.602 | timeout, 3/3 | Lost UNSAT solve |
| `cut_lemmas/15-vars/cut_lemma_01_010` | 2.631 | timeout, 3/3 | Lost UNSAT solve |
| `tightrhombus-273-245-0` | 0.0084 | 0.1491 | Three generated bounds |
| `tightrhombus-273-245-1` | 0.0182 | 13.3826 | 74 generated bounds |

The [followup summary](../../benchmarks/lia-hybrid-20261111/followups/summary.json)
retains all 20 extra timing rows. On the larger rhombus input, the original
hybrid row spends 13.400 seconds in private completion and only 0.0038 seconds
in resumed SAT. This points to repeated arithmetic completion, rather than SAT
call overhead, as the immediate cost.

SLURM job `384852` additionally sampled four hybrid regressions with GDB, using
12-second prefixes, 150 ms jittered intervals and debuginfo downloads disabled.
The [profile record](../../benchmarks/lia-hybrid-20261111/profiles.json) retains
counts, leaf functions and sample hashes. The two cut-lemma prefixes contain
`apply_gomory_cuts` in 76/76 and 74/76 stacks; the larger rhombus has it in
67/77. Rational/tableau checking and GMP operations dominate these sampled
stacks. The PB input instead contains private branch-and-bound in 50/77
stacks and pivots in 66/77. These inclusive counts overlap; they are neither
CPU-event measurements nor full-run percentages. Every prefix hit its intended
deadline and its inferior was reaped. The observations identify remaining
arithmetic costs; they do not establish a general causal explanation or
justify changing cut defaults in this milestone.

### Mechanism observations

Statistics are printed only by normally exiting runs. Killed rows contribute
no final counters, so aggregate counts below cover **143/200 hybrid LIA rows,
280/300 UFLIA rows and 8/12 Certora rows**, not total corpus work. This coverage
also prevents interpreting fewer reported private nodes as a total work ratio.

| Reported hybrid statistic | LIA | UFLIA | Certora repetitions |
| --- | ---: | ---: | ---: |
| Private nodes | 68,929 | 36,060 | 55,670 |
| Private branch conflicts | 9,693 | 35,604 | 55,686 |
| Budget exits / split requests | 11,578 | 26 | 25 |
| Unique generated atoms / SAT resumes | 11,578 | 26 | 25 |
| Peak generated atoms in one solve | 2,243 | 15 | 13 |
| Rejected managed-choice batches | 636 | 7 | 0 |
| Private completion elapsed (s) | 1,019.16 | 129.32 | 226.47 |
| Resumed SAT elapsed (s) | 333.41 | 9.99 | 1.64 |

All reported generated atoms use existing-coordinate registrations. Successful
handoffs use fresh thresholds; reused-bound requests are diagnosed as missing
progress and do not occur in this corpus. The existing integer-slack obligations
remain private. These timers overlap route timers. The unit/integration tests,
rather than aggregate counters alone, establish that ordinary bounds add no
arithmetic rows or variables and preserve split premises in explanations.

## Correctness qualification

The gate ledger is in `docs/run-log.json`, entries
`2026-11-11-lia-hybrid-development`, `2026-11-12-lia-hybrid-qualified-gates` and
`2026-11-12-lia-hybrid-accepted-evidence`. There are no known wrong answers,
invalid models or untriaged reference disagreements in the accepted evidence.
Timeout-only incompleteness is retained explicitly.

- `make check`: 47 CTest groups and 335 Python tests pass. Tests cover exact
  history scores and stable ties, saturating budgets, negative/infinitesimal
  floors, restored bounds, one versus both closed private branches, failed
  preferred SAT choices, multiple handoffs, atom-growth allocation boundaries,
  online/offline UFLIA, both equality producers and scoped sessions.
- ASan: all 47 CTest groups pass with leak and strict-initialization checks.
  Whole-tree clang-tidy covers all 135 tracked C++ translation units with no
  tool errors. The 771 unique advisory diagnostics are not a warning-free-tree
  result. The existing optional, enum-width, leak and missing-direct-header
  findings remain classified; paired self-includes were retained.
- The include-cleaner warning on the Boolean-abstraction executable companion
  exposed a qualification issue: `-DNDEBUG` removed assertions containing
  solver calls. Its direct enum header is retained and assertions now execute
  in Release. Job `384852` compiles the isolated companion with strict Release
  warnings and checks all 24 assignments plus retired-selector UNSAT. This
  corrects the earlier ledger's description of that header as genuinely unused.
- Development differential gate: 4,000 generated inputs, 1,000 per logic under
  each of factor zero and factor one, produce 3,969 passes and 31 timeouts;
  all 2,799 successful native SAT models pass direct Dolmen checks. Current
  snapshot factor-one reruns regenerate 1,000 inputs per logic: 1,982 passes,
  18 LIA timeouts and all 1,394 SAT models validated. These reuse the factor-one
  seeds and are **not 2,000 additional distinct inputs**. No accepted generated
  row has a wrong answer, signal crash, valid-input rejection or disagreement.
- Operator-only real-seed OpFuzz produces 240 LIA matches and 58 UFLIA cases:
  41 matches and 17 cases timing out in both native and Z3. All 170 successful
  native SAT rows pass Dolmen. Twelve selected seeds per logic and a 180-second
  campaign cap do not imply every seed was reached. Earlier TypeFuzz attempts
  could create nonlinear multipliers even with restricted signatures; they are
  rejected gate evidence. A separate replay classified all 232 unrestricted
  saved mutants, with identical baseline/hybrid behavior and no signal crash.
  Job `384853` also classifies all 56 restricted mutants: seven native positive
  error exits agree with Z3 nonlinear-input rejections; the eighth reference
  rejection times out in native. Other incomplete rows are timeouts, including
  all six UFLIA cases. All decisive answers agree, no signal crash occurs, and
  neither restricted run has saved bug files. The
  [classification receipt](../../benchmarks/lia-hybrid-20261111/restricted-typefuzz-triage.json)
  distinguishes the original replay from the shorter two-second diagnostic.
- A branch-limit-one probe with patching/cuts/rounding disabled returns private
  `unknown` and hybrid `sat`; the scoped five-query session validates all four
  SAT models independently. This demonstrates progress past private exhaustion
  and absence of stale restrictions after `pop`.
- All **557/557 main successful SAT timing rows** (264 private, 293 hybrid) and
  **4/4 followup rows** pass separate direct Dolmen replay with the same frozen
  binaries, input hashes and profile arguments. Model time is excluded from
  speed results. Full coverage receipts include zero-SAT slots; no model retry
  replaces an unsuccessful row.

## Reproduction and retained artifacts

The tracked workflows are under `scripts/qualification/lia_hybrid/`.
`check.sbatch`, `asan.sbatch`, `static.sbatch`, `fuzz.sbatch`, `mutations.sbatch`,
`probes.sbatch` and `cli.sbatch` perform the gates. `run.sbatch` invokes `run.py`
with a private configuration supplying corpus roots, output directories and
frozen binaries; public selections provide content hashes, profiles and limits.
`diagnostics.sbatch` compiles the isolated example and samples the regressions.
Validate each script with `slurm_validate_sbatch.py` and run preparation-helper
`--check-only` immediately before submission. Never run these batches on the
workstation or SSH login host.

The timing array was `383804` (512 pairs); regression timings were `384321`
(ten pairs). Main model replay `383996` was held until those repeats completed,
then released with an `afterok:384321` dependency; followup replay was `384331`.
This global barrier keeps both sets of native timings free of validation load.
`export.py` audits model receipt hashes and CSV outcomes, then emits portable
counter/timing rows; `summarize.py` computes the group/family/paired results.

Raw logs and models remain in SLURM workspace
`primo-hybrid-qualified-20261111/results/lia-hybrid-paired` and sibling
`lia-hybrid-followups`, `lia-hybrid-gates` and `lia-hybrid-diagnostics` directories.
The paired and diagnostic archives are also copied to local
`results/lia-hybrid-20261111`. Compact public selections, summaries, model
coverage, counter rows and audit hashes are tracked under
`benchmarks/lia-hybrid-20261111/`. Connection details and absolute deployment
paths are kept outside tracked files. This qualification covers the requested
selections and ordinary splits; broader corpus qualification, factor ablations
and proof-derived combination splits remain future work.
