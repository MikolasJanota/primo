# Reproducible experiments

An experiment needs a question, a fixed input set, exact configurations, and a
decision criterion. The [correctness gate](correctness.md) applies before
performance conclusions. Use [statistics](../reference/statistics.md) to check
that the mechanism under investigation actually ran.

## Prepare the comparison

Record candidate and baseline commits, dirty-tree status, build type, compiler,
CaDiCaL signature, options, input manifest, expected answers, time/memory limits,
and output directory. Check each executable's `--version`; a workspace's Git
revision does not establish which binary was executed. Preserve an exact baseline
binary or build it from an isolated checkout.

The supplied SLURM workflows are reproducible execution options; contributors
can use another adequate test environment while recording the same provenance.
For the maintainer's cluster, use
{project}`workspace preparation <scripts/slurm_prepare_benchmark_workspace.py>`
with `--build-via-slurm` before submitting jobs, as required by the
{project}`workspace execution policy <AGENTS.md#L3-L21>`.
Read its `--help` for connection and staging options.
It synchronizes tracked files; ignored seed corpora must be staged **after each
sync**. Check dependencies and corpus paths on the execution host, not only on
the submission host. Do not substitute a symlink for an in-workspace corpus
when the harness resolves paths relative to that workspace.

Use one output directory per run. Confirm both baseline and candidate execute
correctly through the actual harness before launching the full set. Record
array size and expected rows per shard; a missing shard is not a timeout row.

When the comparison is about timing, run the configurations paired. The
per-solver compare scripts give each configuration its own array tasks, so a
file meets the two configurations on different nodes and at different times.
Node and load differences then look like solver differences near the time
limit; the [QF_LIA progress](../evidence/qf-lia-progress.md) page records such
a case.
{source}`slurm_paired_solver_compare.sbatch <scripts/slurm_paired_solver_compare.sbatch>`
runs every configuration in one array task, on the same node. Each task covers
`PRIMO_PAIRED_CHUNKS` sub-shards and runs the configurations in an order rotated
per sub-shard, so a load change inside the task is spread over all of them. It
writes the usual `<name>/shard-<index>.csv` layout plus the node and CPU model
of each task under `nodes/`. On nodes with simultaneous multithreading,
request two CPUs per task (`--cpus-per-task=2`) so that a run does not share a
physical core with another job.

## Select inputs and interpret outcomes

{toolsource}`analyze_smt2.py <scripts/analyze_smt2.py#L110>` reports declaration counts
and Real-arithmetic shape/polarity summaries:

```bash
python3 scripts/analyze_smt2.py --json path/to/input.smt2
```

The analyzer expands lexical `let` bindings, including nested lets and
shadowing. Quoted and simple spellings of the same name share one binding
(`x` and `|x|`); duplicate equivalent bindings are rejected. Each parallel right-hand side is expanded in the outer environment;
only the body sees the new bindings. For example,
`(let ((x 1) (y x)) (+ x y))` expands to `(+ 1 x)`, preserving the free `x`.
Malformed or duplicate bindings and unsupported binders (`forall`, `exists`,
`lambda`, `match`, and function definitions) produce errors. This tool does
not reproduce frontend/session semantics; consult the
[frontend](../implementation/frontend.md) for the solver's binding rules.

The expansion follows the parallel-substitution semantics of SMT-LIB 2.7,
section 3.6.1, Let, and symbol identity in section 3.1 {cite:p}`smtlib2026`. Its `inline_lets` function owns an explicit
stack of visit/body frames, dictionaries of expanded bindings and fresh list
trees. Right-hand-side frames finish before the body installs bindings;
substituted trees are copied without revisiting their free names. These are the
scope and capture-avoidance invariants checked by the parallel-binding,
shadowing and deep-nesting regressions. Expansion is iterative but can use
exponential time and space when a term is repeatedly copied; direct DAG
traversal remains deferred.

Text search alone cannot establish which theory or transformation a file uses.
Keep corpus exclusions explicit: parse rejection, unsupported constructs,
missing files and reference disagreements cannot silently disappear from the
denominator. An SMT-LIB `:status` is an expected answer, not an independent
proof. Inspect both answer text and process exit status, including multi-query
response counts and abnormal exits after an answer was printed.

Run speed measurements without independent validation in the timed interval.
Then [validate SAT models separately](validation.md), using the same binary,
options and inputs. Stop tuning on a wrong answer, invalid model, crash or
untriaged disagreement. Report timeouts and `unknown` separately from passes.

## Tactic-composition checks

The {source}`composition manifest <benchmarks/tactic-composition-matrix.json>`
defines legal profiles, their logic sets, and required mechanism counters.
The {toolsource}`driver <scripts/tactic_composition_matrix.py#L354>` compares references,
checks answers, and invokes independent model validation. It selects successful
SAT measurements and deduplicates them by profile and resolved input path.
The model stage uses `--expect-sat`: a replay changing SAT to UNSAT fails.

```bash
python3 scripts/tactic_composition_matrix.py \
  --manifest benchmarks/tactic-composition-matrix.json --root . \
  --solver ./build/primo --refs contrib/local/bin/z3 contrib/local/bin/cvc5 \
  --dolmen contrib/local/bin/dolmen --timeout 10 --dolmen-timeout 20 \
  --repeat 1 --out results/composition-review
```

Each profile's `model-validation/coverage.json` joins validation outcomes back
to every selected measurement, including repeats, and records eligible,
validated and incomplete model counts. Missing, duplicate or unexpected
validator rows fail the check, even when the validator exits successfully.
An empty selection reports zero eligible models and skips the validator.
`--allow-inconclusive` permits timeout/unknown replays only as incomplete
validation; it never permits changed answers. Skipping validation does not
complete the qualification gate. Historical results retain their original
selection and must not be relabeled as evidence from this repaired driver.

{source}`The SLURM matrix <scripts/slurm_tactic_composition_matrix.sbatch>`
adds generated cases by logic. Do not require every tactic on every input:
composition can stop after a terminal result. A per-input requirement checks
that the intended route was reached on that case; an aggregate requirement
checks that a tactic was exercised somewhere in the eligible corpus. Preserve
both checks. Report inconclusive rows accepted by `--allow-inconclusive`
separately; that switch does not turn them into successful solves.

## Summarize before deciding

```bash
python3 scripts/summarize_benchmark_shards.py --timeout 30 \
  --run baseline=results/baseline --run candidate=results/candidate \
  --baseline baseline --candidate candidate
```

Inspect expected/completed rows, duplicate inputs, solved counts, PAR2,
recoveries, losses, decisive-answer disagreements, family results, and common
solved timings. PAR2 charges an unsolved instance twice the specified timeout;
it must use the same input set and limit on both sides. For native sessions,
whole-session completion, partial query progress and per-query time are
different measurements. Keep them separate.

An aggregate loss can still reveal a useful portfolio configuration if it
solves inputs the others miss. Record that complementarity, but do not promote
the configuration to default on that basis alone. Record rejected experiments
as rejected; remove their source changes before closing the investigation.

## Evidence storage

The authoritative ledger remains the [run log bundled with this manual](../run-log.json).
Follow the {source}`recording contract <docs/run-log.md>`: stable run ID,
timestamps, kind/scope, exact commit and commands, generator/seed/count,
references, environment/job IDs, limits, artifact paths, summary counts,
evidence status, and limitations. Commit metadata and reproducibility scripts
with the change relying on them. Do not add a solver run entry for a prose-only
documentation build.

Git-based run directories and a derived SQLite query index are an agreed
[future storage design](../work/results-storage.md). They are not current
services. Large raw artifacts may remain outside Git, but the ledger must make
them identifiable and retrievable. A record marked current describes its own
measured revision and scope, not every subsequent commit.

## Refreshing a timing baseline

Use a new experiment workspace and result directory for a complete refresh.
Build the selected clean revision with
the {refreshsource}`preparation helper <scripts/slurm_prepare_benchmark_workspace.py>`
and its `--build-via-slurm` option, finish its
correctness gates, and preserve the candidate and reference binaries. A solver
change requires fresh Primo measurements. Reuse historical reference timings
when their input hashes, binaries, options, time limits and machine/resource
conditions support the intended comparison. Identify their origin explicitly;
do not label reused rows as fresh. Rerun reference timings when requested or
when the existing evidence cannot support the comparison. Run independent
model replay after all timing jobs finish.

The recipes under `scripts/qualification/evaluation_refresh/` support the corpus
checks for this workflow. The {refreshsource}`seed recipe <scripts/qualification/evaluation_refresh/seeds.sbatch>`
checks deterministic ground seeds before timing. The
{refreshsource}`QF_LIA staging recipe <scripts/qualification/evaluation_refresh/stage_qf_lia.sbatch>`
verifies the published checksum of the official
[SMT-LIB 2025 archive](https://zenodo.org/records/16740866) before extracting it into a fresh directory. The caller
supplies the archive and maps `corpora/LOGIC` to each selected input root.
The {refreshsource}`inventory recipe <scripts/qualification/evaluation_refresh/inventory.sbatch>`
records every input's relative path, size and SHA-256, the Primo, Z3, cvc5 and
Dolmen identities, and the allocated machine's CPU information. These are
experiment artifacts, not timing samples.

Use the {refreshsource}`corpus job recipe <scripts/slurm_real_corpus_validation.sbatch>`
and {refreshsource}`benchmark/replay driver <scripts/benchmark_model_replay.py>`
for separate `benchmark` and `replay` phases. They use
`ROOT/LOGIC/{primo,z3,cvc5}/shard-N` result directories. The
{refreshsource}`refresh summarizer <scripts/qualification/evaluation_refresh/summarize.py>` takes `--root`, `--inventory`, `--timeout`
and `--out`; it requires complete timing shards, exact inventory coverage,
matching binary hashes and the intended profiles (`--quiet` for Primo, no extra
reference arguments). To reuse audited references, pass `--reference-map FILE`,
where the JSON file maps each logic to `z3` and/or `cvc5`, each containing `root`
(the directory containing historical `shard-*` directories) and `measurement_note` (the resource and
hardware comparison and its limitations). The same identity checks apply.
Omitted references are reported as unavailable; partial fresh rows do not fill
them. Hardware/resource comparability must be reviewed separately: the script
records the note but does not verify it. Without this option, all three solver
directories are required and labelled fresh.

The report includes family counts, PAR2, solved-runtime median and
nearest-rank 95th percentile, paired Primo/reference ratios and all decisive
answer disagreements. Independent model coverage remains a separate field;
complete timings alone do not qualify models. Timeout and unknown answers are
unsolved and receive the PAR2 penalty. Native session measurements use their
own query-aware drivers and whole-session limits rather than this single-query
adapter. The {refreshsource}`native-session summarizer <scripts/summarize_native_uf.py>`
accepts repeated `--family` options for archive path components; use
`--family hybrid_networks --expected-sessions 10` for the native LRA set.
Its default grouping remains the two native UF families. Its timing and
validation roots must be selected explicitly; it does not verify historical
reference provenance on the caller's behalf.

For the six-division refresh with 1,778 native UF and ten native LRA sessions,
the {refreshsource}`final report job <scripts/qualification/evaluation_refresh/report.sbatch>`
joins the fixed `results/refresh-*` directories after replay. Set `PRIMO_WORKDIR`
and `PRIMO_REPORTING_DIR` to the experiment workspace and its preserved reporting
package. Supply the audited ground reference map and native UF reference-audit
JSON files first. Native LRA reference timing is marked unavailable in this
recipe. The job runs only metadata tools, writes coverage status, and returns
nonzero for incomplete Primo timings, answer failures, disagreements, or missing
supported model qualification. Quantified UF models remain outside independent
ground-model validation. A zero exit does not remove the stated limits of the
correctness samples or the historical timing comparisons.

A refreshed baseline does not erase earlier runs. Retain their revisions and
limitations, and record a new ledger entry naming the fresh source revision,
corpus identities, profiles, limits, jobs, raw artifacts and qualification state.
