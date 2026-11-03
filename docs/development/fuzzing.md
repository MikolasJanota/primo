# Fuzzing and reducing failures

Use the [correctness guide](correctness.md#choose-a-fuzz-gate) to select the
minimum gate. Generated tests and mutations of real inputs cover different
failure modes; neither substitutes for the other. Every solver behavior change
needs fresh evidence for its actual configuration.

## Choose a generator and inspect coverage

| Input family | Maintained entry points |
| --- | --- |
| General QF_UF generation | {source}`fuzz_external.py <scripts/fuzz_external.py>` with FuzzSMT; explicit count/seed/reference/timeout |
| Real and mixed arithmetic | {source}`QF_LRA generator <scripts/qf_lra_boolean_arithmetic_fuzz.py>`, {source}`QF_UFLRA generator <scripts/qf_uflra_smoke_fuzz.py>`, and {source}`LRA model gate <scripts/slurm_qf_lra_model_fuzz_gate.sbatch>` |
| Integer and mixed integer | {source}`QF_LIA model gate <scripts/slurm_qf_lia_model_fuzz_gate.sbatch>` and {source}`QF_UFLIA generator <scripts/qf_uflia_fuzz.py>` |
| Finite domains | {source}`finite_domain_fuzz.py <scripts/finite_domain_fuzz.py>` and its profile matrix |
| Native sessions | {source}`validate_incremental.py <scripts/validate_incremental.py>` and scoped UF/LRA gates |
| Real-seed mutation | {source}`yinyang_fuzz.sh <scripts/yinyang_fuzz.sh>`, with logic-specific configs |

The convenience `make fuzzsmt` target's small default count is not a merge gate.
A skipped generator/dependency is not a pass. Install missing dependencies
where feasible; otherwise record the blocked gate and failed access/install
command. Counts must include valid inputs actually reaching the relevant solver
mechanism, not just files emitted by a generator.

## Seed preparation and mutation limits

Select a small corpus from an available division:

```bash
python3 scripts/build_fuzz_seed_corpus.py \
  --root sandbox/non-incremental/QF_LIA \
  --out benchmarks/seeds/qf_lia_division --per-family 2 --max-bytes 200000
```

The {source}`seed selector <scripts/build_fuzz_seed_corpus.py#L37-L49>` prefers
files below `--max-bytes`, but includes a family's smallest file if none fit.
This is a soft size limit. Record actual sizes and family coverage, and set
the mutation tool's own size limit consistently; otherwise selected seeds may
be skipped. Favoring small files also misses larger shapes. Keep important witnesses such as large
coefficients and proportional polynomial rows; the historical integer defects
show why small-coefficient generation alone is insufficient.

Prepare/sync the workspace first, then stage ignored seeds, and verify them
again before submission. A subsequent tracked-file sync can remove the staged
corpus. Mutation tasks may exhaust their wall budget before visiting all seeds:
report visited seeds, mutations and completed solver calls, rather than claiming
complete seed coverage.

To establish coverage of every seed, use
{settingssource}`slurm_seed_mutation_per_seed.sbatch <scripts/slurm_seed_mutation_per_seed.sbatch>`.
It runs one array task per fuzzer and seed. Seeds are the `*.smt2` files
under `PRIMO_MUTATION_SEEDS` that are no larger than the size limit, in sorted
order. Task $k$ uses fuzzer $\lfloor k/N\rfloor$ and seed $k \bmod N$, where
$N$ is the seed count, so submit `--array=0-(F*N-1)` for $F$ fuzzers. Each
task copies its seed into a private directory, checks the solver banner
against `PRIMO_EXPECTED_GIT`, and writes `task-K/status.tsv`. The status is
`COVERED` when the fuzzer finished all its iterations, `WALL` when the wall
limit cut it short, and `ERROR` otherwise; `ERROR` also fails the task.
OpFuzz runs through {source}`seeded_opfuzz.py <scripts/seeded_opfuzz.py>`,
so both fuzzers get the per-task random seed `PRIMO_MUTATION_RANDOM_SEED_BASE`
plus the task index. The script needs `opfuzz`, `typefuzz` and `z3` on `PATH`.
Bug files still need replay and triage.

Inspect {source}`operator config <benchmarks/fuzz-configs/qf_lia_operator_mutations.txt>`
and {source}`typefuzz config <benchmarks/fuzz-configs/qf_lia_typefuzz.txt>` before describing
their coverage. The older LIA configs intentionally omit several operators;
this is a mutation-coverage restriction, not the current frontend's capability
list. In particular, `div`, `mod`, and `abs` are now accepted extensions and
need dedicated coverage when changed. Malformed or out-of-fragment mutants
must be classified separately; valid input rejected by Primo is a failure.

## Finite-domain activation

The finite-domain generator allocates cases across logics, scenarios and known
SAT/UNSAT outcomes, including nested/residual choices, observed equalities,
definition aliases, shared domains, and transitive domain equalities.

```bash
python3 scripts/finite_domain_fuzz.py --solver ./build/primo \
  --ref contrib/local/bin/z3 --ref contrib/local/bin/cvc5 \
  --logic mixed --count 1000 --seed 4102 --timeout 5 \
  --out fuzz-results/finite-domain-review
```

The `amo` activation profile requires raw and emitted AMO work. Structural
selection uses its own activation profile: selecting and recognizing structure
does not necessarily generate AMO clauses. Mixed Real cases must also reach
Real-equality transformation and mixed dispatch. Some activity requirements
apply per input, others to the whole corpus. Preserve that distinction and
check missing scenarios/rows as well as wrong answers. Validate successful SAT
models in a separate stage. The maintained full profile matrix is
{source}`slurm_finite_domain_fuzz_gate.sbatch <scripts/slurm_finite_domain_fuzz_gate.sbatch>`.

## Triage and reduction

Retain original input, exact binary/options, reference outputs, exit statuses,
seed and limits. Reproduce the failure before reducing it. An old `bugs/`
directory is not current evidence: rerun and classify each retained case.

Write a predicate that preserves the specific defect, including valid input
and the reference answer for a wrong-answer case. Then run:

```bash
python3 scripts/minimize_smt2.py --input failure.smt2 \
  --predicate 'python3 path/to/reproducer.py' --output reduced.smt2 --timeout 5
```

The wrapper invokes ddSMT when installed. If missing, it copies the input and
returns success; inspect its message and do not call that a successful
reduction. Convert a confirmed minimal defect into an original regression test,
then run the required fresh fuzz gate after the fix. A timeout-only case is a
performance investigation; a reference disagreement remains unresolved until
its cause is known.

Record pass, wrong answer, crash, invalid input, valid-input rejection, timeout,
unknown/unsupported, reference disagreement and model outcomes separately.
The [experiment recording contract](experiments.md#evidence-storage) applies
to every gate, including incomplete and rejected runs.
