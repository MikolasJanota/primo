# QF_LIA-only default update

The owner requested M124 + uniform cubes + hybrid branching for QF_LIA,
leaving other logics unchanged. `docs/evidence/qf-lia-defaults-20261119.md`
explains the policy, exact controls, qualification and measurement limits.

`configs/` freezes the correctness profiles, regression manifests and real-seed
mutation candidates. Three distinct 1,000-input generated QF_LIA tasks run the
actual default. Additional tasks run the former profile, M124 with hybrid only,
M124 with cubes only, and unchanged QF_UFLIA. The campaign records exact native
source/hash, seeds, commands, roots and budgets. Timing is a separate card:
`timing.json` retains the established 200-case LIA selection with content hashes.
The four profiles run sequentially in rotated order on one allocated core.
`OLD` uses explicit former settings on the candidate binary to isolate the
configuration effect. No reference timing reruns or global parameter tuning.

Build an isolated source snapshot through
`scripts/slurm_prepare_benchmark_workspace.py --build-via-slurm`, with the
session's private SSH route and prepared dependency installation. Never overwrite
an active workspace. Run `--check-only` immediately before every submission.
The existing held/released controller is
`scripts/qualification/conflict_ackermann/submit.py`; its receipts retain
array counts and validated whole-array dependencies.

Use these existing SLURM methods with a private environment file:

- `euf_performance/verify.sbatch`: `make check` and `make asan-test` array.
- `bound_repairs/static.sbatch`: all tracked non-generated C++ units, using an
  inventory that excludes generated archived log/build trees omitted by the
  preparation helper. Do not drop source units.
- `conflict_ackermann/qualify.sbatch`: generated, tracked/scoped and mutation
  gates using the exact campaign/profiles. Require native and direct Dolmen
  acceptance for every selected successful SAT row.
- `lia_hybrid/run.sbatch`: immutable timing; a separate dependent model phase
  after the whole timing array. `PRIMO_HYBRID_CONFIG` names `timing.json` and
  `PRIMO_HYBRID_METHODS` names the snapshot's `scripts/qualification/lia_hybrid`.

Audit collected qualification rows before starting timing:

```sh
python3 scripts/qualification/conflict_ackermann/audit.py gates \
  --configs benchmarks/qflia-defaults-20261119/configs \
  --rows RESULTS/qualification --out RESULTS/gate-audit.json
```

Any listed mutation unknown requires an exact-profile diagnostic replay; preserve
resource-limited unknowns and timeouts as incomplete outcomes. Stop on wrong
answers, invalid models, crashes or unexplained reference disagreements.
After every selected timing SAT row has a separate direct Dolmen pass:

```sh
python3 scripts/qualification/qflia_defaults/audit.py \
  --config benchmarks/qflia-defaults-20261119/timing.json \
  --topology benchmarks/qflia-defaults-20261119/topology.json \
  --status-inputs benchmarks/qflia-defaults-20261119/status-inputs \
  --rows RESULTS/timing --out REPORT
```

The auditor checks complete row/model sets, ordered arguments, content-pinned
configuration, runner hashes, expected answers and native process status. It
reports solved counts, PAR2, RSS, paired effects, family summaries and available
mechanism counters. One pass cannot establish stable runtime improvements.
Raw artifacts and job IDs are recorded in the qualification record and run log;
private connection details are not part of this reproduction bundle.

`followup-1.json` and `followup-2.json` repeat four completed primary losses
(original slots 59, 62, 76 and 85) with two further profile rotations. Their
32 timing rows are a separate diagnostic set; do not pool them into the primary
200-input solved/PAR2 summary. Audit each repetition with the same report tool,
adding `--followup-of benchmarks/qflia-defaults-20261119/timing.json`. The tool
requires matching parent content/profile identities and identifies the smaller
scope explicitly. Run its model phase only after every timing array finishes.


The retained status input covers the one frozen `unknown` reference label whose
SMT-LIB file declares UNSAT. The auditor checks its content hash and uses the
same declared-status fallback as the native runner. Supplemental Z3/cvc5
corroboration probes timed out at 120 seconds; their rows remain in
`reference-checks.json` and are excluded from speed comparisons.

`topology.json` records all three timing nodes: each observed two-logical-CPU
mask consists of the siblings of one physical core. The topology wrapper reads
sysfs only, under a SLURM allocation. The additional wrappers in
`scripts/qualification/qflia_defaults/` reproduce the documentation-example and
include checks. Prepare `tmp/docs-fixtures/docs/examples/` plus its `scripts/`
checker copy for the former; prepare `tmp/hygiene/` with the current policy and
tactic-test C++ files for the latter. The include check uses the qualified
compile database and compares rebuilt objects byte-for-byte, retaining existing
analyzer signatures from the whole-tree baseline. Both wrappers preserve the
qualified native binary.
