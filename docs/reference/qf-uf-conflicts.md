# QF_UF conflict-output format, version 1

**Status: experimental single-query exporter.** Use `--export-cnf FILE` to
write this format for one QF_UF problem that Primo reports UNSAT. The input may
contain nested Boolean structure. The file records the generated CNF and theory
conflicts; producing it does not independently validate conflicts or certify
UNSAT. Primo bundles no checker; the external Micromath route below covers a
restricted subset of this format.

```bash
./build/primo --quiet --export-cnf certificate.sexp docs/examples/qf-uf-export.smt2
```

The {download}`runnable SMT-LIB example <../examples/qf-uf-export.smt2>` uses
an `or` over an `and`; each possible branch contradicts its EUF disequalities.

## Running the exporter

The option takes just the output filename. Supply one SMT-LIB input file as the
usual positional argument, or supply the script on stdin. The entire script is
buffered and validated before solving. It must contain one `(set-logic QF_UF)`,
followed by declarations, optional `define-fun` commands, and assertions, and
exactly one final `(check-sat)`. An optional `(exit)` and ignored metadata are
accepted. Multiple assertions jointly form the single problem. Push/pop,
resets, SMT-LIB options, model requests, commands after exit, and additional
queries are rejected. The CLI also rejects multiple input files and an output
file equivalent to its input.

The ordinary frontend translates terms, expands definitions, lowers Boolean
equality, and introduces definitions for compound Boolean arguments. The
existing Boolean clausifier handles nested `and`, `or`, `not`, and implication.
The exporter copies **every clause that this clausifier sends to SAT**, including
its Tseitin definitions, root constraints, and truth-constant units. Input need
not already be CNF. It does not independently convert the input to clauses or
implement a new clausification algorithm.

Each theory-atom variable is printed as its SMT-LIB atom. Remaining propositional
variables, including Tseitin auxiliaries, receive fresh declared Boolean names
`primo_cnf_0`, `primo_cnf_1`, and so on, skipping names already used by any symbol
or sort. Their defining clauses are included in the CNF. The normal frontend's
Boolean argument proxies are also declared and their defining clauses are
captured. Thus the artifact is self-contained without exposing SAT variable
numbers. Core simplifications and frontend translation can change source
spelling, order, and duplicates before capture.

The exported CNF is the starting problem for independent checking. Establishing its
relationship to the original formula is still future work; no CNF conversion
proof is emitted. Ground Boolean and object-valued `ite` use the solver's
existing extended-term lowering, including its fresh symbols and defining
assertions. Quantifiers and interpreted sorts remain outside this producer.

Export uses the normal query preparation, preprocessing profile, solver dispatch,
and Boolean clausifier. Engine, propagation, purity-filter and clausification
options retain their ordinary meanings. Incremental assumptions mode, portfolio
solving, branching tactics and preprocess-only mode are incompatible with the
single-CNF artifact. Cancellation and the theory-conflict limit remain applicable.
If preprocessing establishes UNSAT without invoking SAT, the exporter serializes
its false result through the same clausifier, producing an empty clause and no
theory lemmas. `:export.preprocessing-unsat=1` marks this case. Such a small
artifact is not evidence that the source-to-CNF conversion has been certified.

An UNSAT result writes one complete S-expression to `FILE`, replacing existing
contents. A SAT or unknown result does not write the path; a pre-existing file
therefore remains unchanged and must not be mistaken for this run's artifact.
Rejected requests also leave the path untouched. An output open/write failure
reports an error result and a nonzero exit status; a failed write may leave a
partial file. Normal solver results stay on stdout and diagnostics on stderr.
With `--stats`, `:export.cnf.clauses` counts captured input/definition clauses and
`:export.theory.conflicts` counts retained conflicts, including duplicates.
`:export.theory.conflicts-collected` and `:export.theory.conflicts-removed`
distinguish the original collection from reduction.

The producer at `cf82b726` passed 48 CTests, 301 Python tests, 1,000 fresh
reference-checked cases, 48 ASan tests, and six complete Micromath example
replays with negative controls. The {download}`qualification report
<../coverage/cnf-export-streaming.json>` records jobs, commands, hashes, and the
empty-clause reference-validator repair. Real-corpus proof coverage has the
separate limits below.

## Reducing theory lemmas

```bash
./build/primo --quiet --export-cnf certificate.sexp --export-cnf-reduction=core input.smt2
./build/primo --quiet --export-cnf certificate.sexp --export-cnf-reduction=mus input.smt2
```

The reduction option requires `--export-cnf`. The default `none` retains all
collected conflicts. `core` uses one failed-assumption core query in a separate
SAT instance; it returns an UNSAT subset that may contain redundant lemmas.
`mus` starts from that core and removes lemmas until every remaining one is
necessary. This is **subset minimality relative to the fixed input CNF**, not
minimum cardinality. Different subsets can be valid answers. If the input CNF
alone is propositionally UNSAT, both modes return no conflicts.

The separate oracle asserts the captured input clauses permanently and guards
each learned theory clause with a fresh activation variable. Assuming a
variable activates its clause. These variables are private to reduction and
never appear in the artifact. All queries are propositional: asking an SMT
solver would make every valid theory lemma redundant by theory reasoning.
Reduction changes only the conflict-record selection, preserving their order,
literal contents, the input CNF and signature. It does not minimize individual
conflicts or independently check their theory validity.

The reducer owns a fresh incremental-safe SAT backend for one export and reuses
its search across subset queries. It does not enable incremental SMT-LIB input.
Cancellation applies during reduction; an interrupted request returns unknown
and writes no artifact. Main-solver CaDiCaL options and theory-conflict limits
are not applied to the postprocessor. There is no separate reduction budget.
Reduction adds retained raw clause copies and another SAT instance to memory
use and can require many SAT calls.

With `--stats`, `:export.reduction.sat-calls` counts actual reduction solves and
`:export.reduction.core-removed` counts initial core removals. For `mus`,
`:export.reduction.block-queries`, `:export.reduction.binary-queries`, and
`:export.reduction.max-block-size` expose the progression search;
`:export.reduction.completed=1` marks a completed requested reduction.
The [design and literature note](../leads/theory-lemma-reduction.md) maps
CAV 2013 Algorithms 3–4 to the implementation and records selector-free
alternatives. Specialized model-guided algorithms are not implemented.

## Temporary object-sort erasure

```bash
./build/primo --quiet --export-cnf certificate.sexp --experimental-export-erase-sorts --export-cnf-reduction=mus docs/examples/qf-uf-export-multisort.smt2
```

`--experimental-export-erase-sorts` is a temporary, disabled-by-default option
for testing the external checking framework. It requires `--export-cnf` and is
intended to be removed when the framework supports the original sorts. The
signature contains one fresh `primo_uf_sort` declaration, adding a numeric suffix
to avoid all original sort/function names. Every uninterpreted object sort in
function arguments and results is printed as that common sort. `Bool` and all
function identifiers remain unchanged. Exactly one object sort is declared even
if the original problem has zero or one.

The solver and reducer retain the original many-sorted terms and SAT clauses.
Only declaration text changes; CNF and conflict literal sequences are preserved.
For the supported ground QF_UF fragment this is equisatisfiability-preserving,
by the model construction in the [design note](../leads/experimental-sort-erasure.md).
The same argument preserves the inconsistency of individual conflict records.
Quantified and interpreted-sort inputs remain unsupported. No independent
erasure-proof object is emitted.

With `--stats`, `:export.sort-erasure.enabled=1` marks the rewrite and
`:export.sort-erasure.input-sorts` counts original object sorts. The
{download}`multi-sort example <../examples/qf-uf-export-multisort.smt2>` uses a
function from one object sort to another and becomes acceptable to Micromath
with this option. It does not remove that checker's separate restrictions on
Boolean arguments, positive-arity Boolean-valued functions or Boolean conflict facts.

## Checking with Micromath

[Micromath's SMT tools](https://git.olsak.net/mirek/Micromath/src/commit/ac052d97ea441ca52cfd0a6b904fad8d34098a8e/smt/README.md)
consume this container directly. The current tested pin is
`ac052d97ea441ca52cfd0a6b904fad8d34098a8e`. It fixes the saved Goel weakening-mask failure and
adds `--no-factor` to proof generation. All eighteen certificates in the six-input
stress set now have independent kernel proofs (job `345415`); see the
{download}`update report <../coverage/micromath-fixes.json>`.

Earlier qualification used `df5c5d3` and `505588a`; the
{download}`SAT-optimization comparison <../coverage/micromath-sat-update.json>`
retains their historical counters. Those measurements are not attributed to the
new pin. Its [input specification](https://git.olsak.net/mirek/Micromath/src/commit/ac052d97ea441ca52cfd0a6b904fad8d34098a8e/smt/Primo.md)
supports exactly one uninterpreted object sort, functions from objects to
objects, and propositional Boolean variables in CNF. Conflicts contain object
equalities/disequalities or truth constants. Multiple object sorts,
positive-arity Boolean-valued functions, Boolean function arguments and Boolean variables in
conflicts are outside this checker's fragment. The structured example above is
within it; the extended format example below is not.

Download the external checkout into the ignored dependency directory:

```bash
git clone https://git.olsak.net/mirek/Micromath.git contrib/src/micromath
git -C contrib/src/micromath checkout ac052d97ea441ca52cfd0a6b904fad8d34098a8e
```

Build Micromath with `make` in that checkout and provide a CaDiCaL executable
with text LRAT support. Its SMT regression suite needs Python 3.10 or newer;
`MICROMATH_PYTHON` selects an interpreter for the batch workflow.
`MICROMATH_REUSE_BUILD=1` explicitly reuses already built external binaries;
the report records that choice and their hashes. On this project's cluster, building and batch checking
belong in SLURM. The optional {micromathworkflowsource}`Micromath workflow <scripts/slurm_micromath_check.sbatch>`
builds both external tools inside its allocation, runs Micromath's `make
check-smt`, and checks fresh artifacts from all three export-reduction modes, both for
the ordinary single-sort example and the erased multi-sort example.
Its environment parameters select separate external source directories, a
prepared Primo binary, and a results directory; connection details stay outside
the script. Prepare Primo with
{micromathworkflowsource}`slurm_prepare_benchmark_workspace.py <scripts/slurm_prepare_benchmark_workspace.py>`
with `--build-via-slurm` and perform its
`--check-only` preflight before submission. The workflow never copies external
implementation code into Primo.

For a single artifact, these are the proof stages. Set `certificate`,
`proof_dir`, and `cadical` to absolute paths, and run from the Micromath checkout:

```bash
mkdir -p "$proof_dir"
./build-axioms --fragment equality "$proof_dir/axioms.umd"
./smt/build-challenge "$proof_dir/axioms.umd" "$certificate" "$proof_dir/challenge.umd"
./smt/prove-cnf "$certificate" "$proof_dir/problem.cnf" "$proof_dir/problem.lrat" --cadical "$cadical"
./smt/prove-lrat "$proof_dir/challenge.umd" "$certificate" "$proof_dir/problem.lrat" "$proof_dir/solution.ump"
./check-solution "$proof_dir/challenge.umd" "$proof_dir/solution.ump"
```

`build-challenge` encodes the input CNF without assuming the supplied conflicts.
The prover derives those conflicts and combines them with the LRAT replay.
Successful independent replay prints `challenge proven`. The evaluated replay
path handles RUP hints in textual LRAT; RAT hints and binary encoding remain
outside its supported fragment. This proves the
encoded exported CNF unsatisfiable under Micromath's equality axioms; the
relationship between that CNF and the source SMT-LIB script still needs the
conversion evidence discussed above. A conflict-only check with
`./smt/check-conflicts` does not establish propositional sufficiency.

CaDiCaL's bounded variable addition can introduce fresh variables into LRAT,
which this RUP-only checker does not support. In `ac052d9`, `prove-cnf` disables
BVA itself by passing `--no-factor`; the separate wrapper used with earlier
revisions is no longer needed. When generating a trace outside `prove-cnf`, use
`--lrat --no-binary --no-factor`. Existing traces containing extension variables
must be regenerated. The saved SEQ trace is still rejected with an explicit
unsupported-fresh-variables diagnostic; the update does not add general RAT or
extension-variable replay. The changed option applies to the separate proof
SAT solver, leaving Primo's main solve and lemma reduction unchanged.

The workflow also checks three rejection cases: a consistent alleged conflict,
an insufficient lemma collection, and a corrupt proof file. Exact revisions,
commands, job status, proof hashes and scope are recorded in
the {download}`run report <../coverage/micromath-check.json>` and the run ledger. No broader-fragment or
performance claim follows from this integration test.

### Historical real-benchmark stress pilot

The original {micromathpilotsource}`pilot harness <scripts/micromath_stress.py>` and
{micromathpilotsource}`SLURM job <scripts/slurm_micromath_stress.sbatch>` select
up to twelve metadata-UNSAT SMT-LIB QF_UF inputs from six families at two size
ranks, with a 1 MiB input cap. Each is tried with `none`, `core`, and `mus`,
using experimental object-sort erasure. A Z3 `sat` answer flags a disagreement
with UNSAT metadata and blocks that case. Reference `unknown`, timeout or error
is recorded without blocking proof attempts. Export, conflict validation,
challenge construction, LRAT/proof generation and kernel replay have separate
logs and outcome states. Only successful kernel replay counts as a verified
profile; a profile-stage timeout or error overrides any partial answer.

Four workers use 3 GiB per-process memory ceilings and 256 MiB per-file limits.
Each reference call has a 20-second limit; each profile shares a 60-second
pipeline budget, including at most 30 seconds for export, plus a two-second
termination grace for a timed-out process. The {download}`pilot report
<../coverage/micromath-stress.json>` records sampling rules, revisions, job
status and results. The {download}`selected manifest <../../benchmarks/micromath-pilot-20261103.json>`
pins the twelve inputs by path, size and hash. Job `339676` completed: three
inputs reached kernel replay in all modes (nine verified profiles), six timed
out during export (eighteen profiles), two encountered Micromath's unsupported
positive-arity Boolean declarations (six profiles), and one required exporter
`ite` lowering (three profiles). All twelve reference answers were UNSAT; no
wrong answer, invalid conflict or invalid completed proof was observed.
The TypeSafe successes declare seven object sorts before erasure. All modes
retained the same lemmas on inputs whose export completed, so this sample shows
no minimization benefit. This is a bounded diagnostic sample, not full-corpus
coverage or a performance comparison.

### Stress inputs selected from known-fast results

The current {download}`stress harness <../../scripts/micromath_stress.py>` requires a hash-pinned manifest drawn
from recorded native UNSAT results. The pilot selects results within one second;
the uncapped sweep described below accepts all recorded completed UNSAT results. The
{download}`selected manifest <../../benchmarks/micromath-fast-unsat-20261103.json>`
contains six inputs of 94–821 KB; their historical answers, timings and input
hashes are retained. The harness no longer chooses cases
by size alone. {download}`select_micromath_fast_cases.py <../../scripts/select_micromath_fast_cases.py>` selects 64 KiB–2 MiB source
files, largest first with at most two per family, verifies historical input
hashes, excludes the previous pilot, and conservatively screens declared
Boolean function arguments and positive-arity Boolean results. This lexical
screen is not a complete SMT-LIB parser or a proof of checker compatibility.
`ite` is allowed through the normal lowering pipeline. The selector can join
`shard-*/answers.csv` and `provenance.json` with `--history DIR
--historical-source-git REV`, or consume prejoined `--records FILE`; it checks
completed, unchanged-input records and confirms the supplied source revision
matches the recorded binary version. Both forms run corpus selection in SLURM.

A five-second `none` export is the admission check. If it fails to establish main
UNSAT, CORE/MUS runs are skipped for that input. Reduced exports have a 30-second
limit; downstream stage limits and the shared 60-second profile budget remain.
Historical native timings and current export/checking timings are separate
measurements. Large source bytes alone do not establish stress on the proof
framework: exported CNF size, collected/retained lemmas and proof sizes must also
be reported.

Job `345296` exported all six selected inputs in all three modes. Each reference
answer was UNSAT; no native timeout, wrong answer, crash or rejected input was
observed. Exports, including requested reduction, took approximately 0.6–2.5
seconds and contained 8,623–36,171 CNF clauses. These are actual SAT-boundary
problems, rather than preprocessing-only false goals. The historical timings
select inputs; they are not a comparable speed baseline.

| Input | CNF clauses | Lemmas: none / core / MUS | Full replay evidence |
| --- | ---: | ---: | --- |
| Goel peg solitaire 2, br | 11,985 | 336 / 115 / 95 | All modes kernel-verified at `ac052d9` |
| Goel peg solitaire 5, fp | 15,073 | 249 / 82 / 52 | All modes kernel-verified at `ac052d9` |
| QG `gensys_icl_sk006` | 36,171 | 820 / 575 / 526 | All modes kernel-verified at `ac052d9` |
| QG `gensys_icl006` | 35,826 | 773 / 556 / 525 | All modes kernel-verified at `ac052d9` |
| SEQ005 size9 | 9,064 | 513 / 461 / 404 | All modes kernel-verified at `ac052d9` |
| SEQ038 size9 | 8,623 | 1,179 / 754 / 573 | All modes kernel-verified at `ac052d9` |

All eighteen initial profiles passed conflict checking and CaDiCaL reported
propositional UNSAT, but their default LRAT traces introduced fresh variables;
full Micromath proof construction failed. Those initial attempts are not counted
as kernel proofs. Jobs `345309` and `345343` regenerated compatible traces with
`--no-factor`, reusing the certificates and challenges, and established twelve
kernel proofs for the four QG/SEQ inputs. Source solving was not repeated.
On the same SEQ005 MUS trace, Mirek's update reduced SAT kernel work from
1,448,860 to 800,848 steps; both versions' proofs replayed successfully.

The Goel weakening-mask error persisted in `df5c5d3` and `505588a`, including
plain traces (job `345317`). It is fixed at `ac052d9`: the exact saved Goel
challenge/LRAT now replays, and newly generated proofs pass in every mode for
both Goel inputs. Regenerating the traces for the same eighteen saved certificates
produced no extension variables, and all kernel checks printed `challenge proven`.
The full upstream `make check` suite and three independently authored rejection
controls also passed in job `345415`. No SMT solving was repeated. This is
six-input coverage, not a full-corpus result or a runtime speedup comparison.

The {download}`stress and diagnosis report <../coverage/micromath-fast-stress.json>`
records per-phase outcomes, resources and proof hashes. The
{download}`Goel checker input <../../benchmarks/checker-regressions/micromath-20261103/goel-weakening-mask.primo>`
and {download}`SEQ checker input <../../benchmarks/checker-regressions/micromath-20261103/seq-extension-vars.primo>`
are saved with {download}`reproduction notes <../../benchmarks/checker-regressions/micromath-20261103/README.md>`.
They are MUS-reduced certificates, not failures minimized for the checker.
A local self-contained archive adds the exact challenge/LRAT files and a replay
script; its path and hash are in the report. Both replay payloads reproduced
the expected errors after relocation in job `345343`; the final archive adds
only its root metadata manifest to those unchanged replay files.

### Expanded sample across five families

Job `345465` checked twenty additional files: four each from Goel, QG, SEQ,
PEQ and eq_diamond. The {download}`expanded manifest
<../../benchmarks/micromath-expanded-20261103.json>` retains historical native
UNSAT timings within one second and exact source hashes. Selection requested
24 files and found twenty eligible inputs of 13,193–640,983 bytes, using an
eligibility interval of 4 KiB–2 MiB and at most four per family. CNF/lemma/proof sizes
are in the {download}`expanded results <../coverage/micromath-expanded.json>`.

The selector now accepts repeated `--exclude-manifest` arguments, excludes both
paths and content hashes, and deduplicates selected hashes. `--min-bytes`,
`--max-bytes` and `--max-per-family` control the bounds; their defaults remain
64 KiB, 2 MiB and two per family. This run used `--count 24 --max-per-family 4
--min-bytes 4096 --max-bytes 2097152` and excluded both earlier manifests.

| Mode | Kernel proofs at the initial export limit | Export timeouts |
|---|---:|---:|
| `none` | 20/20 | 0 |
| `core` | 20/20 | 0 |
| `mus` | 17/20 | 3 |

All 57 completed certificate pipelines reached exact kernel confirmation.
The three MUS timeouts were QG `gensys_icl057`, `gensys_icl068` and
`gensys_icl085`: main UNSAT took 0.30–0.57 seconds, then precise reduction
exhausted the 30-second export limit. CORE passed on all three. This run used
a shared 40-second profile budget, four workers, 3 GiB address space and a
256 MiB per-file limit per worker, in a 4-CPU/16-GiB SLURM allocation. Reference
Z3 returned UNSAT on seventeen inputs and timed out at ten seconds on three;
these reference timeouts are separate from successful kernel proofs. No
checker rejection, crash or answer disagreement was observed. This extends
supported-subset coverage; it does not establish source-to-CNF correctness or
full-corpus coverage.

Job `345488` retried only the three MUS exports with a 120-second limit.
Two completed in 41 and 50 seconds and passed full kernel replay. The remaining
`gensys_icl057` export again reached main UNSAT in under a second, then timed out
in reduction without publishing a certificate. Across both jobs, 59 of the
sixty distinct profiles are kernel-verified: all twenty `none`, all twenty
`core`, and nineteen `mus`. Original timeout attempts remain in the report.
The retry used three workers with the same address/file caps, a 7-minute
allocation, and a separate 180-second replay budget for each completed export.

The expanded CNFs contain 1–26,491 clauses. Four eq_diamond inputs reduce to a
single empty clause in preprocessing; these cover a boundary rather than
large theory proofs. The Goel sokoban input also needs no theory lemmas.
Per-input retained lemma counts and exact checked artifact hashes are in the
report. Full proof files remain in the SLURM workspace; local result mirrors
contain metadata, logs and all 59 checked `input.primo` certificates, whose
hashes were verified after transfer.

### Preparing a CORE-only corpus sweep

The {download}`selector <../../scripts/select_micromath_fast_cases.py>` accepts
`--all-known-unsat` to remove the pilot's historical one-second, source-size,
per-family and total-count caps. This flag overrides those cap options. It still
requires successful recorded native UNSAT results, checks historical source
hashes, deduplicates content, and conservatively excludes declared functions
with Boolean arguments or positive-arity Boolean results. It does not select
known timeouts or SAT inputs. Exclusion manifests are optional and repeatable;
omit them to include previously checked inputs in a complete sweep.

Run selection inside SLURM, using the recorded native run directory and revision:

```bash
python3 scripts/select_micromath_fast_cases.py --history "$history" --historical-source-git "$native_revision" --corpus "$corpus" --all-known-unsat --out "$manifest"
```

The {download}`check driver <../../scripts/micromath_stress.py>` accepts
`--modes core` to export and check CORE certificates directly, without a NONE
admission solve or a MUS run. Its default remains `none core mus` for existing
pilots. Experimental sort erasure is enabled for these exports. Source inputs
outside Micromath's remaining fragment can still be reported as unsupported;
the lexical declaration screen does not establish full compatibility.

`--shard-count N --shard-index I` assigns manifest positions congruent to `I`
modulo `N`, preserving original case numbers. Every shard must use the same
immutable ordered manifest, with exactly one task for each index `0..N-1`.
Each shard uses a fresh output directory. The
{download}`CORE sweep wrapper <../../scripts/slurm_micromath_core_sweep.sbatch>`
defaults to array `0-3%4`, four CPUs and 16 GiB per task, and a two-hour allocation.
Set the same dependency/corpus/manifest variables as the pilot wrapper;
`PILOT_OUT` now names the fresh sweep root, with `shard-I` children. If overriding
`--array`, set `CORE_SHARD_COUNT` to the matching total and use contiguous
zero-based indices. Create `logs/` and run the canonical preparation
`--build-via-slurm`/`--check-only` preflight before submission.

The wrapper runs only CORE, with four workers per shard. It accepts
`CORE_REFERENCE_TIMEOUT` (default 20 seconds), `CORE_EXPORT_TIMEOUT` (30 seconds),
and `CORE_PROFILE_BUDGET` (60 seconds shared across export and proof stages).
Downstream stage limits remain 10/10/15/30/10 seconds for conflict checking,
challenge construction, CNF generation, proof construction and kernel replay,
with two seconds of termination grace. Each worker and child has 3 GiB address
space and a 256 MiB per-file cap. Timeouts remain distinct from checker errors;
export statistics retain main-UNSAT evidence even when reduction times out.
Reference statuses are recorded separately and never replace kernel checking.

The parent owns counters and the per-case journal. After each completed case,
it atomically publishes `cases/NNN/result.json`, appends and flushes/fsyncs the
full row to `rows.jsonl`, then atomically publishes `summary.json`. Profile
checkpoints also use atomic replacement. `--compact-summary` (enabled by the
wrapper) omits full rows from the summary and avoids retaining completed rows
in RAM; their detail remains in case files and the journal. A killed write may
leave a final partial journal line; the individual JSON checkpoints remain
readable. This is durable progress reporting, not automatic resume: reruns
require a fresh output directory. Summaries report assigned and completed case
counts, requested profiles, family outcomes, references and shard identity.
Check every shard's completion counts before aggregating; scheduler success
alone does not mean every certificate was proven.

The wrapper defaults to `--keep-success-artifacts certificates`: after exact
kernel confirmation and hashing, it removes successful CNF, challenge, LRAT and
kernel-proof intermediates while retaining `input.primo`, logs and metadata.
Hashes/sizes and `removed_success_artifacts` identify discarded payloads.
Unsuccessful profiles keep their generated artifacts for diagnosis. Set
`CORE_KEEP_SUCCESS_ARTIFACTS=all` to retain successful proof payloads too; the
driver itself defaults to `all`, preserving pilot behavior. These per-file caps
do not bound total campaign disk use, so size the sweep storage accordingly.
No native producer or checker algorithm changes are involved.

Array `345785` qualified this route on two previously checked inputs, one per
shard: both CORE certificates reached kernel confirmation, with no NONE/MUS
exports. Four focused workflow tests passed in each task, covering uncapped
selection, Boolean-signature exclusion, disjoint sharding, timeout statistics
and release of completed result rows. Successful intermediates were removed
and both certificate hashes verified after collection. The
{download}`workflow qualification report <../coverage/micromath-core-sweep-workflow.json>`
records exact scripts, tool hashes, limits and an initial checksum-setup failure.
This qualifies the workflow; the full corpus sweep has not been launched.

### Replaying saved certificates without Primo

The {download}`replay driver <../../scripts/micromath_replay.py>` reads saved
`input.primo` files below `--inputs`, creates new equality axioms, challenges,
DIMACS/LRAT traces and proofs, and independently replays each proof. It invokes
the external tools through the existing
{preparedexportsource}`process runner <scripts/micromath_stress.py>`. It never
runs Primo or mutates saved input files. Supply a fresh `--out` directory:

```bash
python3 scripts/micromath_replay.py --micromath "$micromath" --micromath-git ac052d97ea441ca52cfd0a6b904fad8d34098a8e --cadical "$cadical" --inputs "$saved_certificates" --out "$proof_dir" --workers 4
```

Run the driver within SLURM. `--reuse-lrat` instead reads each input's adjacent
`challenge.umd`, `problem.cnf` and `problem.lrat`, preserving the exact old
challenge/trace bytes for regression diagnosis. The supplied CaDiCaL executable
is hashed for provenance in both modes; saved-trace mode does not execute it.

A parent process owns result records and atomically replaces `summary.json`;
workers own separate output directories and synchronous subprocess stages.
Logs and generated proof files remain on disk. Hashing streams files through
1 MiB buffers. The driver also records input/challenge/trace hashes, tool hashes,
DIMACS/LRAT variable counts, command lines, exit statuses and phase times.
It checks that each certificate's hash remains unchanged through replay.
This is an original orchestration workflow, not another SAT or theory checker.

Each of at most four workers and its children has a 3 GiB address-space limit
and 256 MiB per-file limit. Subprocess stages share a 180-second budget, with
individual limits of 15 seconds for conflicts/challenge, 30 for CNF generation,
90 for proof construction and 45 for kernel replay, plus a two-second
termination grace. Subsequent metadata parsing and hashing are outside that
pipeline budget; the allocation's wall limit bounds the complete job.
`kernel_proven` requires exit zero and the exact `challenge proven` output;
`Inconsistency proven!`, a mere zero exit, errors and timeouts do not count.
Other outcomes retain their stage logs, and the driver exits nonzero unless
every input is kernel-verified. These limits and the saved/regenerated modes
were exercised in job `345415`.

## Container and literal syntax

A file contains exactly one S-expression, with these sections occurring once in
the order shown. Keywords and the version are literal tokens; `*` below means
zero or more entries and is not part of a file.

```text
(primo-conflicts
  (version 1)
  (logic QF_UF)
  (signature declaration*)
  (cnf (clause literal*)*)
  (conflicts (conflict literal*)*))
```

The wrapper is custom syntax, not an SMT-LIB command script. Inside it, use
SMT-LIB lexical conventions, including whitespace, semicolon line comments, and
quoted symbols such as `|left endpoint|`. Section names are reserved only at
their structural positions. Version 1 has no additional sections or attributes.

The signature contains `declare-sort` commands for uninterpreted sorts of arity
zero and `declare-fun` commands for constants and functions. `Bool`, `true`,
`false`, equality, and Boolean operators have their usual SMT-LIB meanings.
Declare sorts before functions using them. Declare each user symbol once and
declare every symbol referenced anywhere in the CNF or conflicts, including
auxiliary symbols. Use the same signature for both sections. No definitions,
quantifiers, arithmetic, assertion-stack commands, or solver commands occur in
the container.

A literal is a ground Boolean atom or `(not atom)`. An atom is `true`, `false`,
a Boolean constant or uninterpreted Boolean-valued application, or binary
equality `(= term term)` between terms of the same sort. Terms must be ground
and well-sorted QF_UF terms; Boolean arguments and Boolean equality retain their
SMT-LIB meaning. Boolean expressions inside term arguments do not introduce
extra outer clause connectives. At clause level, use the wrapper's literal
sequence rather than an `or` term. Express disequality as `(not (= s t))`;
`distinct` and nested outer negations are not literal forms in version 1.

## Meaning and polarity

The CNF denotes the conjunction of its clauses. A clause denotes the disjunction
of its literals. An empty CNF is `true`; `(clause)` denotes `false`.

A conflict records **conflicting facts**: its literals are claimed to have an
inconsistent conjunction in QF_UF, independently of the input clauses. For
example:

```lisp
(conflict (= a b) (not (= (f a) (f b))))
```

The facts say that `a` and `b` are equal but their function results differ.
Congruence makes this impossible. Negating each fact gives the corresponding
theory-valid clause:

```lisp
(or (not (= a b)) (= (f a) (f b)))
```

For a conflict sequence with literals l₁, …, lₙ, the intended theory lemma is
¬l₁ ∨ … ∨ ¬lₙ. Negation here is semantic complementation: complementing
`(not atom)` yields `atom`. The conflict itself is not a clause to conjoin with
the input CNF. No explanation steps, equality paths, or congruence witnesses
accompany it.

An empty `(conflict)` claims that the theory alone is inconsistent. It is
structurally representable but is not a valid conflict in ordinary QF_UF.
Similarly, syntactic well-formedness does not make any other recorded conflict
valid. An empty conflicts section records no conflicts and makes no SAT claim.

Preserve literal order, record order, duplicates, and repeated conflicts.
They have no logical significance; version 1 requires no sorting, minimization,
or deduplication. Conflicts may mention declared atoms absent from the input
CNF. They must still be inconsistent without assuming input clauses.

The intended eventual use is that the input CNF together with the complemented
conflict sequences is propositionally UNSAT. Version 1 neither demonstrates
that property nor guarantees that the recorded collection is sufficient.

## Examples and boundary cases

This complete artifact records a congruence conflict:

```lisp
(primo-conflicts
  (version 1)
  (logic QF_UF)
  (signature
    (declare-sort U 0)
    (declare-fun a () U)
    (declare-fun b () U)
    (declare-fun f (U) U))
  (cnf
    (clause (= a b))
    (clause (not (= (f a) (f b)))))
  (conflicts
    (conflict (= a b) (not (= (f a) (f b))))))
```

The downloadable {download}`extended example <../examples/qf-uf-conflicts.sexp>`
also covers transitivity, opposing equality and disequality, multiple sorts,
predicates, Boolean arguments, a quoted name, duplicate literals, and repeated
conflicts. Its input CNF already contains the congruence contradiction above;
the additional conflicts illustrate the format and need not form a minimal set.

These are complete structural boundary examples:

```lisp
; Empty conjunction: true. No recorded conflicts.
(primo-conflicts
  (version 1) (logic QF_UF) (signature) (cnf) (conflicts))

; Input CNF contains false, so it needs no theory lemmas.
(primo-conflicts
  (version 1) (logic QF_UF) (signature) (cnf (clause)) (conflicts))

; Well-formed container, but its empty conflict is an invalid theory claim.
(primo-conflicts
  (version 1) (logic QF_UF) (signature) (cnf) (conflicts (conflict)))
```

Each S-expression above is a separate file. Semantic review cases for a future
consumer include reversing a conflict's polarity, omitting a required equality
premise, undeclared symbols, and ill-sorted equality. These must not be treated
as valid theory evidence merely because parentheses balance.

## Representation and integration

The public representation is typed term text and literal sequences. It contains
no Primo term IDs, SAT variable numbers, flattened terms, congruence-class
representatives, or references to solver memory. There is no ordering or
numbering dependency on Primo's allocation history. An independent consumer
needs only this specification and SMT-LIB term semantics.

The standalone {preparedexportsource}`smtlib::ConflictDocument <src/smtlib/conflict_document.hpp>` and
{preparedexportsource}`print_conflict_document <src/smtlib/conflict_document.cpp>` remain available for independently authored
text documents. The solver exporter instead owns a {preparedexportsource}`smtlib::ConflictSpool <src/smtlib/conflict_spool.hpp>`
with its {preparedexportsource}`implementation <src/smtlib/conflict_spool.cpp>`: three buffered anonymous temporary files for
signature, CNF, and conflicts. It stores conflict byte offsets and lengths only
when reduction needs selection. `TMPDIR` can select node-local scratch. Securely
created temporary files are unlinked immediately and closed on every return.
Temporary-storage failure aborts export; it does not fall back to text in RAM.

{preparedexportsource}`export_qf_uf_cnf <src/smt/qf_uf_export.cpp>` owns one `Frontend`, its term
manager, the spool, and collector through solving and serialization. It validates
the whole command sequence and QF_UF sorts, then calls
{preparedexportsource}`check_sat_frontend_export_once <src/smt/solver_session.cpp>`. That entry point
borrows the frontend and runs the same production preparation and dispatch as
ordinary queries. Signature printing waits until collection binds, so declarations
include fresh symbols introduced by the existing preprocessing/lowering passes.

{preparedexportsource}`BooleanClausifier::ClauseObserver <src/smt/clausifier.hpp>` is an optional
synchronous callback at the clausifier-to-SAT boundary. One insertion helper
invokes it and forwards the same clause to the backend. The observer copies data
it retains and must not mutate the clausifier, term manager, or backend, or reenter
clausification. Both the Boolean and EUF solver leaves accept a collector; without
one they retain ordinary clause insertion and early-return behavior.

{preparedexportsource}`QfUfConflictCollector <src/smt/qf_uf_export.hpp>` borrows the frontend's term
manager and spool. It copies initial clauses as SAT literals while the clausifier
builds its atom mapping. After Boolean argument registration, binding copies the
SAT-variable-to-TermId mapping, assigns fresh Boolean auxiliary names, and streams
initial clause text. Later clausifier emissions are rejected. The current ground
EUF path does not introduce new atoms after binding.

Lazy conflicts and model-propagation clauses are recorded at
{preparedexportsource}`insertion <src/smt/solver_engines.cpp>`. The
{preparedexportsource}`online propagator <src/smt/solver_engines.cpp>` records external conflict clauses and reason clauses when their
terminating zero is requested by SAT. Thus queued but unused propagation reasons
are not exported. Duplicate delivered clauses may remain. No SAT-learned clause
is exported. The SAT adapter sees only propositional literals and has no exporter
or core-term dependency.

Primo's [EUF explanations](../implementation/euf.md#follow-one-conflict) already
use learned-clause polarity. The collector complements those literals to produce
the conflicting facts specified here. An unmapped theory literal is an error.
Each term is streamed by {preparedexportsource}`print_ground_term <src/smtlib/term_printer.cpp>`,
using an iterative traversal without a cache of expanded subterm strings.
No expanded atom, clause or conflict strings are retained by collection.

Collection costs expected linear time in captured literals plus names and expanded
term output. Printing repeated shared terms can still produce large files and
consume substantial CPU. The spool retains bounded I/O buffers and optional
per-conflict offsets, rather than all output text. Numeric CNF copies remain in
memory for CORE/MUS; `none` releases initial raw clauses after binding and stores
no raw theory clauses. The original script and core term DAG remain in memory.
After main UNSAT, the existing SAT-only reducer returns indices; it never reads
text or spools. Final publication copies selected byte ranges through a bounded
buffer. Empty selection emits no conflicts; `none` emits every conflict.

The spool is sealed and flushed before the output path is opened. Spooling errors
therefore leave a pre-existing artifact unchanged, just as SAT/unknown and
interrupted reduction do. A failure during final publication can still leave a
partial output file. `:export.main-unsat` distinguishes the main result from
postprocessing; `:export.solve-and-collect.us`, `:export.format-input.us`,
`:export.format-conflicts.us`, `:export.reduction.us`, `:export.final-write.us`,
and `:export.spool.{signature,cnf,conflict}-bytes` expose work and staged byte counts.
Solve/collection time includes the formatting counters, so these times must not
be summed. See the [storage design note](../leads/cnf-export-spooling.md).

{lemmareductionsource}`CheckSatOptions::export_cnf <src/smt/check_sat.hpp>` and
{lemmareductionsource}`check_sat_smtlib <src/smt/check_sat.cpp>` expose the same buffered
single-query route to library callers. Direct `SmtSession` construction rejects
a nonempty export filename because that streaming API cannot validate the
whole script before a query. The reduction option without an export filename
is also rejected by the library entry point. Incremental queries, branching and
source-to-CNF conversion proofs require further producer design. The original standalone-format examples remain illustrative,
rather than captured exporter output.

This page is an original output-format and adapter design note. The adapter
reuses Primo's normal solver pipeline and the ground term printer; it introduces no new
congruence-closure or proof algorithm. The external checking route above covers
the stated subset; a checker for the full format remains future work.
