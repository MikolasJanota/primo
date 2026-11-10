# QF_LRA weighted conflict certificates, version 2

**Status: experimental implementation with bounded SLURM qualification.**
`--export-cnf FILE` now accepts one QF_LRA query using the same single-query
command/session restrictions as [QF_UF export](qf-uf-conflicts.md). QF_UF keeps
its version-1 format; QF_LRA writes version 2. The artifact alone takes the prepared CNF
as its starting problem. The [default Micromath stress route](#exported-cnf-verification) constructs its
statement from that exported CNF. The separate
[original-source route](#micromath-verification-from-the-original-source)
constructs its statement from the original SMT-LIB and proves its own Boolean
conversion, using the retained arithmetic conflicts.
The [design note](../leads/lra-conflict-certificates.md) gives the mathematical
contract, reconstruction argument and implementation checklist.
The separate [integer-only LIA format](qf-lia-certificates.md) adds bounded
experimental export with named, multi-step cut derivations. Its
[design note](../leads/lia-conflict-certificates.md) compares the representations.

```bash
./build/primo --quiet --export-cnf result.sexp --export-cnf-reduction=core docs/examples/qf-lra-export.smt2
python3 scripts/check_lra_certificate.py result.sexp --sat-solver /path/to/cadical
```

The {download}`example <../examples/qf-lra-export.smt2>` is the strict-boundary
contradiction shown below.
The second command needs an external CaDiCaL executable for the propositional
check; omitting `--sat-solver` checks only the arithmetic witnesses and syntax.

CORE and MUS select complete theory clauses before any coefficients are
reconstructed. `none` reconstructs every collected lemma; it does not retain
formatted text or a vector of all numeric lemmas in RAM. Empty selection needs
no reconstruction. SAT or inconclusive results write no artifact and leave
an existing output untouched. Reconstruction failure is not a SAT result.

## Small examples for checker development

The {download}`starter archive <../../benchmarks/checker-regressions/lra-starter-20261104/lra-starter.tar.gz>`
contains 12 original small SMT-LIB problems and their exact exported
certificates (149–844 bytes each), ordered from weak/strict bounds through
integer and rational scaling to signed literals, Boolean branches and native
equality/`ite` lowering. The first certificate is a Boolean-only control.
Three separately labelled authored controls exercise acceptance of zero weights
and rejection of broken cancellation or missing strictness.

The archive includes a standalone guide, file hashes and a standard-library
Python verifier. The immutable archive retains its original guide from before Micromath's LRA
support; the [current compatibility results below](#micromath-verification-from-the-original-source)
qualify these exact saved files. The
{download}`guide <../../benchmarks/checker-regressions/lra-starter-20261104/README.md>`
explains the polarity convention and works through the rational-weight example.
Native generation and independent source/witness/propositional checks ran in
SLURM job `348286`; the producer is the qualified implementation `e910cfde`.
Job `348287` checked the exact archive's hashes and confirmed all 13 accepted
files (including the zero-weight control) and both expected rejections, with
external propositional checking enabled.

## Format and supported fragment

```lisp
(primo-conflicts
  (version 2)
  (logic QF_LRA)
  (signature (declare-fun x () Real) (declare-fun y () Real))
  (cnf (clause (<= (+ x y) 2)) (clause (>= x 2)) (clause (> y 0)))
  (conflicts
    (conflict-lra ((<= (+ x y) 2) 1) ((>= x 2) 1) ((> y 0) 1))))
```

Declarations are Real or Boolean constants, including auxiliaries introduced
by the ordinary preparation/clausification path. The initial fragment rejects
uninterpreted sorts/functions, Int/mixed arithmetic, quantifiers and incremental
commands. Sort erasure is restricted to the QF_UF path. Existing Real-equality
and extended-term lowering are reused, so there is no new exporter-specific
equality or `ite` translation.

The streaming {lraexportsource}`smtlib::print_ground_term <src/smtlib/term_printer.cpp>` printer supports rational literals,
linear sums/scalings and comparisons as well as its original ground UF terms.
It prints negative rationals with SMT-LIB unary minus and retains no cache of
expanded subterm strings. The separate quantified UF string printer remains
restricted to UF.

Each `conflict-lra` pair contains a signed comparison literal and a nonnegative
exact rational coefficient, printed as an integer or `(/ NUM DEN)` with positive
denominator. Zero coefficients and all original selected premise positions
remain present. The facts are conjunctively inconsistent; their complements
form the theory-valid clause to add to the Boolean abstraction of the CNF.
After normalizing comparisons to $a_i x\le b_i$ or $a_i x<b_i$, the checker
requires cancellation, nonnegative multipliers, and a negative constant sum or
a zero sum with a positive-weight strict premise. No floating approximation or
infinitesimal appears in the artifact.

## Implementation, ownership and failure

The generic {lraexportsource}`smt::CnfConflictCollector <src/smt/cnf_export.hpp>` replaces the
formerly UF-specific collector. Both Boolean/UF and LRA leaves bind it to the
actual clausifier observer. LRA collection covers complete external conflicts,
propagation reasons when their terminating zero is delivered, and lazy
fallback conflicts. No weights are stored by the normal LRA search.

The collector borrows the query's term manager, owns atom-ID/polarity bindings,
and keeps numeric CNFs only when subset reduction needs them. Its
{lraexportsource}`NumericClauseSpool <src/smt/numeric_clause_spool.hpp>` owns an anonymous C `tmpfile`, native-binary literal records
and, for reduction modes, record offsets. `none` uses sequential reading with
no offset index. These are private query-local records, not a portable format.
This numeric temporary file uses the C library's temporary-file location; the
existing text spool separately honors the repository's temporary-directory
mechanism. All temporary files close when their query owner is destroyed.

For each retained lemma, the collector normalizes only that lemma's facts and
passes sparse exact coefficients to
{lraexportsource}`theory::arithmetic::reconstruct_lra_certificate <src/theory/arithmetic/lra_certificate.hpp>`.
A fresh Bland tableau solves the
non-strict multiplier problem below. Here the rows of $M$ are normalized
premise coefficient vectors, $b$ contains their right-hand sides, $s_i$ is one
for a strict premise and zero otherwise, and $\lambda_i$ is that premise's weight.

$$
\lambda\ge0,\quad M^T\lambda=0,\quad b^T\lambda\le0,
\quad(s-b)^T\lambda=1.
$$

Its model supplies ordinary rational weights. A separate exact accumulator
checks the candidate before text is staged. The temporary arithmetic state is
released between lemmas. The {lraexportsource}`tableau <src/theory/arithmetic/lra_tableau.hpp>`
accepts an optional cancellation callback
inside its pivot loops and throws `LraCheckInterrupted`; the reconstruction
boundary returns an explicit interrupted status rather than interpreting it as
SAT. Non-export solving passes no callback and retains its former behavior.

Reconstruction has a separate fixed 60-second cooperative budget for the whole
phase, plus the CLI cancellation callback. It polls during record/affine setup,
between pivots, while extracting models and before completion. Individual
large exact-number operations and an individual formatting operation are not
hard real-time bounded; process/allocation limits remain the external bound.
All weighted records are staged before opening the final path. Invalid or
unsupported witnesses return an export failure with main-UNSAT diagnostics;
interruption returns unknown. Final-publication I/O failures can leave a partial
file, as in version 1. There is no resumable or incremental export.

Statistics distinguish `:export.main-unsat`, reconstruction selected/finished
lemma counts, entries, nonzeros, pivots, completion and microseconds, plus the
numeric-spool and weighted-text bytes. A CORE/MUS removal count is not an
arithmetic proof-validity result. `entries` includes zero-weight positions;
`nonzeros` measures the normalized premise matrix's support, not nonzero weights.
Later independent propositional verification
must use the exact retained clauses and atom mapping.

## Independent validation and qualification scope

The {download}`Python checker <../../scripts/check_lra_certificate.py>` independently
parses the serialized facts and uses standard-library `Fraction` arithmetic,
without accessing the producer's normalized matrix or private tableau. It
checks signed-literal interpretation, affine syntax, declarations, rational
syntax, coefficient signs, cancellation and strictness. Its optional
`--sat-solver` invocation checks propositional UNSAT of CNF plus complemented
theory conflicts using an external DIMACS solver. That invocation trusts the
SAT solver; it is not a kernel verification of a propositional proof.
The external Micromath route below produces kernel-checked proofs for the
listed source/certificate pairs. An Ethos translation is not implemented.

This initial standalone checker parses the entire artifact in memory. It is a
qualification/diagnostic tool, not a bounded-memory streaming proof checker.
The native producer stages text/numeric records on disk and reconstructs one
selected lemma at a time; its subset oracle and query term manager still consume
memory. No new performance or full-corpus coverage claim is made here.

The native tests cover strict/non-strict feasibility, fractions and duplicate
coefficient entries, zero-support premises, infeasible systems, interruption
inside one tableau check, numeric-spool access and preservation of a pre-existing
artifact when reconstruction is cancelled. Python tests mutate coefficients,
constants, polarity, strictness and syntax. `tests/lra_export_cli.py` is the
SLURM reference/witness/propositional validation driver.

At implementation revision `e910cfde`, SLURM qualification has passed 49 native
tests and the Python suite (318 tests, two skips), 1,000 fresh generated LRA inputs plus
eight focused cases, and 1,000 fresh UF export regressions. All 807 UNSAT LRA
artifacts passed exact witness checking and external propositional confirmation;
the 201 SAT cases created no artifact. ASan also passed all 49 native tests.

A separate CORE replay selected 12 real inputs from recorded native UNSAT
results of at most one second, checking unchanged source hashes and limiting
selection to three files per family. All 12 passed the reference, witness and
propositional checks. Seven artifacts have nonempty arithmetic conflicts;
five need no theory lemmas after propositional reduction. CORE retained 1,440
of 3,220 collected lemmas, so 1,780 lemmas required no coefficient reconstruction.
The largest retained set has 1,147 lemmas. These are bounded correctness checks,
not full-corpus coverage or a solver performance comparison. The
{download}`qualification report <../coverage/lra-certificate-export.json>` records
the jobs, limits, source/input/binary hashes, commands and rejected attempts.

## Sources and implementation status

The implemented baseline follows the original normalized multiplier construction
in the [design note](../leads/lra-conflict-certificates.md), using Motzkin's theorem
{cite:p}`LeikeHeizmann2015Ranking` (§6.1) for strict/non-strict witness existence.
CPC {cite:p}`ReynoldsEtAl2026CPC` motivates explicit scaling, summation and
normalization obligations; neither CPC nor ranking-template synthesis is
implemented. The core/tableau numerical implementation and native SAT adapter
are reused, with no external solver source copied. Source-to-CNF proof, native
equality premises, coefficient/literal minimization, LIA, proof-kernel validation
of the external SAT run, and proof-language translation remain outside this
implementation.


## Exported-CNF verification

The {arithmeticcnfsource}`arithmetic stress runner <scripts/arithmetic_certificate_stress.py>`
now defaults to `--statement-source=exported-cnf`.
The checker proves UNSAT of the certificate's CNF together with its independently
checked theory lemmas. Primo's transformation from the original SMT-LIB to that
CNF remains trusted. This matches the certificate format's starting-problem
contract; it does not establish an original-source preprocessing proof.

The {arithmeticcnfsource}`streaming adapter <scripts/certificate_cnf_statement.py>`
reads a native version-2 LRA or version-3 LIA artifact. It writes its declarations
and one SMT-LIB assertion per exported clause, followed by `check-sat`. Empty
clauses become `assert false`; an empty CNF produces no assertions. Nonempty
clauses include a harmless `false` disjunct, so unit clauses have a binary `or`
as required by SMT-LIB. The adapter consumes but never asserts any conflict or
cut. Thus a theory lemma cannot become an unchecked source assumption.

The adapter retains only its current lexical token and nesting counters while
streaming the file. It preserves quoted identifiers, including newlines, and
rejects unsupported envelopes or malformed section structure. It is a transport
adapter for native output, not a general SMT-LIB parser or an arithmetic checker.
It refuses to overwrite an existing statement and removes partial output on
handled errors; an externally killed conversion may leave a partial file.
The runner requires successful conversion before invoking the challenge builder.
Original sources, native certificates, statement hashes and selected statement
mode remain recorded. Raw proofs still stream directly to the kernel.

For example, use the generated statement wherever the original-source example
below supplies its SMT-LIB input:

```bash
set -o pipefail
python3 scripts/certificate_cnf_statement.py result.sexp prepared.smt2 --logic QF_LRA
./smt/build-lra-challenge axioms.umd prepared.smt2 challenge.umd
./smt/prove-lra challenge.umd result.sexp - --raw --cadical /path/to/cadical | \
  ./check-solution challenge.umd -
```

Run the first command from Primo and the remaining commands from Micromath, with
appropriate absolute paths. The LIA route uses `--logic QF_LIA`, its LIA tools,
and the existing explicit version-2 envelope copy for the prover; the adapter
reads the unchanged native version-3 file.

For the {arithmeticfeedbacksource}`SLURM wrapper <scripts/slurm_arithmetic_certificate_stress.sbatch>`,
`ARITHMETIC_STATEMENT_SOURCE=exported-cnf` selects this default.
`ARITHMETIC_STATEMENT_SOURCE=original` selects the stronger diagnostic route.
An optional `ARITHMETIC_SELECTION_ROOT` supplies one hash-pinned selection JSON
per logic, avoiding changes to the benchmark set during a paired comparison.
Both modes check the original input with the selected reference solver and perform native CORE export
before building the selected challenge. They record verification counts
separately. A successful exported-CNF proof is never counted as an
original-source proof.

### Qualification and remaining producer work

Array `362734` verified six LRA and six LIA cases in a focused 14-case set,
including both new shared/nested ITE regressions. A Boolean-evaluation failure
and the known LIA reconstruction limit remained. An earlier attempt, `362732`,
exposed an adapter bug: unit clauses used unary `or`. It is superseded by the
corrected qualification, which uses the false disjunct described above.

Array `362736` then repeated the same 48 real input hashes as `362720`, using
native binary `a5123159` and the same pinned Micromath builds:

| Exported-CNF outcome | LRA | LIA |
| --- | ---: | ---: |
| Kernel proof verified | 24 | 17 |
| Native reconstruction incomplete | 0 | 1 |
| Reference timeout; export skipped | 0 | 1 |
| Challenge timeout, 30 seconds | 0 | 1 |
| Proof/kernel timeout, 60 seconds | 0 | 2 |
| Closed-Boolean evaluation error | 0 | 2 |

Sixteen LRA successes and thirteen LIA successes used retained theory
conflicts. These counts establish 41 real exported-CNF proofs; they are not
an increase in original-source proof coverage. The two authored ITE regressions
are additional distinct inputs from qualification, not part of the 48-case set.
The {download}`exported-CNF evidence ledger <../coverage/arithmetic-exported-cnf.json>`
records input and statement hashes, exact workflow/binary hashes, family/stage
outcomes, resource limits and retained artifact locations.

The remaining producer limitation is `dillig/25-22.smt2`: CORE retained one
lemma, but integer proof reconstruction reached a resource limit after 5,224
generated cuts and 5,186 projection pairs. The source-based ITE diagnostics,
sort mismatches, source parsing errors and missing-refutation diagnostics did
not recur in this exported-CNF pilot. The remaining checker errors and limits
are preserved for external feedback; no checker implementation was changed.
The [opposing-bound follow-up](../leads/lia-conflict-certificates.md#opposing-bound-reconstruction-follow-up)
records two rejected producer-side attempts. This producer limitation remains
open; no native algorithm change was accepted.

### Yices reference and independently validated checker feedback

The arithmetic SLURM wrapper now defaults to `contrib/local/bin/yices-smt2`.
Provision it with `make contrib-yices`, or set `ARITHMETIC_REFERENCE` to an
existing Yices SMT-LIB executable. The standalone runner still takes an explicit
`--reference` path. The wrapper records its version and the runner records the
selected binary name and hash. Historical measurements above still used Z3.

SLURM `362756_1` checked Yices 2.7.0 on the previous reference-timeout source
`pidgeons/pigeon-hole-19.smt2` and the shared/nested ITE control. Both returned
UNSAT, exported native CORE certificates and reached `challenge proven`.
This is a targeted two-case qualification, not a repeated 48-case sweep or a
general reference-speed comparison.

SLURM audit `362757` independently checked the five saved Micromath failures:
Yices returned UNSAT on both each original input and its exported-CNF statement;
Python integer-cut/type replay plus independent CaDiCaL confirmed propositional
UNSAT; statement regeneration matched the saved bytes; and every LIA
compatibility copy matched its native body with only byte 28 changed.
These checks found no producer-side defect. They do not formally prove
preprocessing or checker soundness.

Both `Averest/parallel_prefix_sum/ParallelPrefixSum_safe_bgmc002.smt2` and
`ParallelPrefixSum_safe_bgmc003.smt2` freshly reproduced
`Logic failure: evaluation needs a closed Boolean term`, with producer and
kernel exits `[1,1]`. Both certificates have zero theory conflicts, so no
supplied integer-cut derivation is involved. The observation is specific to
pinned Micromath `8f9cff132140462b141183ddb9cb88a9beb80a4f`; no upstream fetch or
checker implementation inspection/modification was performed.

The {download}`217 KiB smallest error replay <../../benchmarks/checker-regressions/micromath-lia-feedback-20261107/micromath-lia-small-error.tar.gz>`
contains the smaller unchanged error instance, its original input, native and
explicitly adapted certificates, checker statement, logs, manifest and replay
helpers. The
{download}`6.9 MiB full feedback bundle <../../benchmarks/checker-regressions/micromath-lia-feedback-20261107/micromath-lia-feedback.tar.gz>`
contains both reproduced errors plus all three independently validated resource
cases. Those three retain the original `362736` challenge/proof deadlines and
logs; their checker resource runs were not repeated during this audit. They are
workload-and-budget feedback, not evidence of a correctness bug. Neither bundle
contains raw proofs, checker binaries or saved challenge dumps. These are
original cases, not delta-minimized inputs. No message or files were sent to
the external author.

After extracting the small archive, run on an allocated Linux compute node:

```bash
bash replay.sh /path/to/Micromath /path/to/cadical \
  014-closed-boolean /fresh/log/directory
```

The {arithmeticfeedbacksource}`portable replay script <scripts/replay_micromath_lia_feedback.sh>`
builds the challenge from `exported-cnf.smt2`, reads the explicit version-2
certificate copy, and streams `--raw` into the kernel. Its two error replays
were exercised during the audit. It bounds axiom/challenge construction by
30 seconds and each proof/kernel process by 60 seconds, with 3 GiB address
space and 256 MiB file caps per child. The original pilot used one overall
60-second proof/kernel deadline; this script uses individual process deadlines.
An error exit is expected for these feedback cases and is not proof acceptance.

The {arithmeticfeedbacksource}`audit driver <scripts/audit_arithmetic_checker_failures.py>`
and {arithmeticfeedbacksource}`audit SLURM wrapper <scripts/slurm_arithmetic_checker_audit.sbatch>`
export only independently validated cases. A changed error replay is excluded
from the archive. The
{download}`feedback ledger <../coverage/micromath-lia-feedback.json>` records
input, binary, workflow and archive hashes, actual stage results and limits.
Our separate `dillig/25-22.smt2` reconstruction limit remains unresolved and
is not presented as a Micromath failure.

### Larger recorded-fast LRA sweep

Array `362761` expands the LRA input set to 217 files from 11 families, with
Yices 2.7.0 as the reference and the same native `a5123159` and Micromath
`5f22700410c0e37044c03557dbd21e009303b7ba` binaries as the qualified workflow.
It reserves all 24 previous LRA sources as controls, adding 193 distinct inputs.
The {download}`frozen selection <../../benchmarks/qf-lra-certificate-sweep-20261107.json>`
records every input hash, historical source/binary identity, family, size and
baseline-control flags and shard count. All selected inputs had unchanged hashes and recorded
native UNSAT times of at most one second at `5efbd022`, using `--quiet` and the
default preprocessing profile. Those historical timings are selection criteria,
not measurements of the current certificate workflow.

The cohort is all 217 eligible files in the configured 4 KiB–8 MiB interval,
from 403 recorded-fast UNSAT rows; 186 rows fall outside that size interval.
The selector's count target was 256, with a family ceiling of 256, so it
exhausted the eligible pool without excluding a family. Actual source sizes
are 4,337–890,062 bytes. Size-band counts are 136 below 32 KiB, 75 from 32 to
128 KiB, four from 128 to 512 KiB and two at least 512 KiB. The sweep increases
input coverage substantially but does not claim balanced size coverage or
multi-megabyte source coverage.

The {lrasweepsource}`selector <scripts/select_arithmetic_certificate_sweep.py>`
uses the {lrasweepsource}`shared sampling routine <scripts/arithmetic_certificate_stress.py>`:
reserve hash-matched controls, visit families round-robin within size bands,
then fill unused capacity deterministically. It retains the conservative
single-query/no-positive-arity-declaration screen. The committed manifest is
the authoritative set for replay; this is not the full QF_LRA corpus or the
full historical solved set.

The {lrasweepsource}`sweep wrapper <scripts/slurm_lra_certificate_sweep.sbatch>`
has separate selection and run stages. Selection `362760` writes the full
manifest, eight disjoint round-robin shards and their hashes. Each run task
checks its shard hash before invoking the
{lrasweepsource}`shared arithmetic wrapper <scripts/slurm_arithmetic_certificate_stress.sbatch>`
with explicit `ARITHMETIC_LOGIC=QF_LRA`, a per-shard selection file and an isolated
output directory. The original two-logic pilot interface remains supported.

Run-stage allocation is two CPUs and 12 GiB per shard, at most four concurrent
shards, with a two-hour wall limit. Per-case limits remain 15 seconds for Yices,
30 each for native export, statement conversion and challenge construction,
and 60 for the proof/kernel pipeline. Each external process has a 3 GiB address
limit and 256 MiB file limit. CORE is applied before witness reconstruction;
raw proof steps stream directly to the independent kernel. Successful challenge
dumps are hashed then removed; native certificates, statements and failed-stage
logs remain available. As elsewhere, only both zero exits plus exact
`challenge proven` count as verification, and the original-to-CNF step is trusted.

The first selector attempt, `362758`, failed before any instance was run because
Python 3.9 lacks `itertools.pairwise`; the sampler now checks adjacent size bands
without that import. Selection `362759` produced 190 cases with a draft family
cap of 64 and was superseded before solver execution. The final selection
`362760` includes all 217 eligible inputs. Native C++ and its active unit tests
are unchanged; these are workflow and corpus-selection changes only.

Array `362761` completed all eight tasks with zero scheduler exit codes. The
{download}`result ledger <../coverage/qf-lra-certificate-sweep.json>` records
per-case outcomes, retained lemma counts, payload hashes, binary identities and
scheduler accounting:

| Outcome | Cases |
| --- | ---: |
| Independent kernel verified | 214 |
| Proof/kernel pipeline reached its 60-second deadline | 3 |
| Reference, native export, statement or challenge failure | 0 |
| Previous controls verified | 24 / 24 |

Of the 214 verified cases, 146 retain arithmetic lemmas and 68 need none after
preprocessing/CORE. Across all 217 exports, CORE removes 23,540 of 37,867
collected theory lemmas, leaving 14,327. This is an UNSAT core, not a MUS.
No wrong answer or reference disagreement appeared. The successful runs verify
the exported-CNF statement; they do not establish the source-to-CNF step.

The three deadline cases are:

| Case | Exported-CNF bytes | Native certificate bytes | Retained lemmas |
| --- | ---: | ---: | ---: |
| `clock_synchro/clocksynchro_5clocks.worst_case_skew.induct.smt2` | 32,400 | 237,503 | 163 |
| `clock_synchro/clocksynchro_6clocks.worst_case_skew.induct.smt2` | 39,742 | 392,768 | 291 |
| `LassoRanker/SV-COMP/BradleyMannaSipma-2005ICALP-Fig1_true-termination.c_Iteration1_Lasso_5-phaseTemplate.smt2` | 4,274,470 | 3,931,219 | 222 |

Input byte size alone does not explain every deadline. No attribution to a
particular checker phase, memory limit or checker defect follows from these
three pipeline timeouts. The checker implementation was not inspected.

The {lrafeedbacksource}`feedback wrapper <scripts/slurm_lra_sweep_feedback.sbatch>`
ran as job `362769`, using the same Yices and CaDiCaL binaries. For each saved
case it freshly confirmed UNSAT with Yices on both the original source and the
exported CNF, regenerated the CNF byte-for-byte, checked every arithmetic
witness with exact rational arithmetic and confirmed propositional UNSAT.
All three passed. No producer defect was found by those checks. The Micromath
stages were not repeated in this audit: their observations remain the original
bounded-workload results, not freshly reproduced correctness bugs.

The {download}`474,019-byte replay archive <../../benchmarks/checker-regressions/lra-sweep-feedback-20261107/lra-sweep-feedback.tar.gz>`
contains the three unchanged sources, exported CNFs, CORE certificates, original
logs, audit results, payload hashes and manual replay commands. Start with the
five-clock case for the smallest input. See the
{download}`replay instructions <../../benchmarks/checker-regressions/lra-sweep-feedback-20261107/README.md>`.
The cases have not been delta-minimized. Challenge dumps, raw proofs and checker
binaries are omitted; the replay builds the challenge and pipes `--raw` proof
steps into the kernel. All 76 manifest-listed payload hashes were verified after
download. Full per-case artifacts remain under
`$PRIMO_WORKDIR/results/lra-certificate-sweep-362761/shard-N/run`, and the audit
under `results/lra-sweep-feedback-362769`.

### Separate proof export from kernel replay

The {lracapturesource}`capture driver <scripts/capture_micromath_lra.py>` diagnoses the three larger-sweep deadlines
without changing Micromath. Its input is the original sweep directory plus the
independent audit summary. It verifies saved input/certificate/statement hashes
and the producer, kernel and CaDiCaL binary identities, then reuses the retained
challenge. Failed challenges were not hashed by the original sweep; their hashes
are first recorded by this diagnostic run, rather than claimed as previously
verified identities.

The {lracapturesource}`capture function <scripts/capture_micromath_lra.py#L35>` owns a private producer process group, reads its `--raw`
stdout in 64 KiB chunks and writes gzip at compression level 1. It uses bounded streaming buffers,
a 4 KiB tail and rolling hash/counts, with one-second progress
records on disk. Export runs without a kernel reader, so kernel backpressure
cannot stall this stage. A successful export becomes `proof.umd.gz`; a handled producer error,
internal deadline or size cap closes `proof.partial.umd.gz` normally, marks it
incomplete and excludes it from full kernel replay. External job termination
or an I/O exception may interrupt gzip finalization or summary publication; a
partial filename alone does not establish a readable, recorded prefix. The private process group, including
CaDiCaL descendants, is killed when capture ends.

Only a complete export is decompressed through a pipe into `check-solution` in
a second stage. Verification requires both decompressor and kernel zero exits,
exact `challenge proven`, and no inconsistency marker. The {lracapturesource}`SLURM wrapper <scripts/slurm_capture_micromath_lra.sbatch>` assigns two CPUs and 12 GiB to each
of three concurrent tasks, with a 15-minute allocation and separate 300-second
export and replay limits. External processes retain the 3 GiB address and
256 MiB file caps. Default capture bounds are 2 GiB of raw proof and near
256 MiB compressed output, reserving space for gzip buffers/trailer. The
`CAPTURE_RAW_BYTES` and `CAPTURE_GZIP_BYTES` overrides change these two capture
bounds only; they do not change the external process limits. No full
uncompressed proof is written or retained in memory. These stage timings include
compression/decompression and extra I/O; they diagnose where work accumulates
and are not a direct timing comparison with the original 60-second live pipe.

Submit only after the canonical qualified-workspace preflight, with the
independently audited inputs and frozen helper directory available on the
compute nodes:

```bash
export PRIMO_WORKDIR=/path/to/qualified-workspace
export CAPTURE_WORKFLOW=/path/to/frozen/scripts
export LRA_SWEEP_RESULTS="$PRIMO_WORKDIR/results/lra-certificate-sweep-362761"
export LRA_SWEEP_FEEDBACK="$PRIMO_WORKDIR/results/lra-sweep-feedback-362769"
export MICROMATH_LRA_DIR=/path/to/pinned-micromath
export PRIMO_SAT_SOLVER=/path/to/lrat-capable-cadical
cd "$PRIMO_WORKDIR"
sbatch "$CAPTURE_WORKFLOW/slurm_capture_micromath_lra.sbatch"
```

The frozen helper directory must contain `capture_micromath_lra.py`,
`micromath_stress.py` and the wrapper from the same recorded workflow. Results
appear in `results/lra-proof-capture-$SLURM_ARRAY_JOB_ID/case-N`. The default
array handles all three cases; `sbatch --array=1 ...` selects only the five-clock
case. A larger diagnostic disk budget can be supplied as, for example,
`CAPTURE_RAW_BYTES=17179869184 CAPTURE_GZIP_BYTES=4294967296 sbatch --array=1 ...`
(16 GiB raw, 4 GiB compressed; the stage deadline remains 300 seconds).

The {download}`capture ledger <../coverage/micromath-lra-proof-capture.json>`
records the completed diagnostics. Initial array `362773` stopped all three
captures at the default compressed-file bound after 24–28 seconds, with
556–683 MB raw byte prefixes. Those finalized partial files were not replayed
or counted as proofs. Array `362776_1` then exported the five-clock case with
16 GiB raw and 4 GiB compressed bounds; `362777_0,2` did the same for the other
two. All three full exports completed and all three separate kernels accepted.

| Case | Raw proof GB | Gzip GB | Export seconds | Kernel replay seconds |
| --- | ---: | ---: | ---: | ---: |
| Five clocks | 2.207 | 1.062 | 93.35 | 35.52 |
| Six clocks | 3.691 | 1.778 | 155.88 | 59.40 |
| LassoRanker | 2.560 | 0.914 | 97.87 | 34.21 |

GB here means one billion bytes; exact counts and hashes are in the ledger.
The five-clock exported CNF is only 32,400 bytes, its native certificate is
237,503 bytes and it retains 163 lemmas after CORE. The large artifact is the
expanded binary Micromath proof. Gzip reduces these proof streams substantially
but still leaves roughly gigabyte-sized files. Export itself takes longer than
the old 60-second live-pipeline budget in each staged run; the separate kernels
accept all three complete proofs. These observations establish a proof-volume
and budget issue for this pinned workflow, without identifying a faulty checker
implementation or claiming directly comparable pipeline timings.

Job `362779` fully decompresses all six saved gzip files (three complete, three
partial), checking gzip CRC, raw byte counts and raw SHA256. The
{lracapturepackagesource}`packaging wrapper <scripts/slurm_package_lra_capture.sbatch>`
then writes a {download}`743,023-byte feedback archive <../../benchmarks/checker-regressions/lra-proof-capture-20261107/lra-capture-feedback.tar.gz>`
containing the five-clock source/CNF/CORE certificate, compressed saved challenge,
stage logs/results, progress records and only the first 1 MiB of its raw proof,
gzipped. That proof prefix is a sample, not a complete certificate. See the
{download}`replay instructions <../../benchmarks/checker-regressions/lra-proof-capture-20261107/README.md>`.
Complete compressed proofs remain on SLURM under
`$PRIMO_WORKDIR/results/lra-proof-capture-362776/case-1` and
`results/lra-proof-capture-362777/case-{0,2}`, with hashes and byte counts recorded
in both the ledger and archive manifest. They are not committed to Git.

The first diagnostic array, `362770`, failed before producer launch because
`/usr/bin/time` was unavailable; the workflow now uses Python `getrusage` and
Bash's `time` builtin. Reaped-child CPU/RSS counters exclude Python compression
CPU and may omit unreaped descendant usage; they are not total allocation
measurements. The byte-stream newline counters are not counts of binary proof
steps. No native solver code, checker implementation or upstream checkout was
changed. The original-to-CNF trust boundary and the previous independent
Yices/witness/SAT audit remain as described above.

### Replaying the author's checker update

The supplied `external/Response.md` reports successful replays of the five LIA
feedback cases. It attributes the two closed-Boolean errors to Boolean proof
reconstruction, and describes changes to Boolean schema reuse, encoding,
clause dependencies, arithmetic normalization and source-clause transport.
These are the author's implementation claims, not a source-code audit by Primo.
The response's identity is recorded by SHA256 in the result ledger.

We fetched and pinned
[Micromath `cfd36f46e756f080295d469753c9f2989dab1fee`](https://git.olsak.net/mirek/Micromath/src/commit/cfd36f46e756f080295d469753c9f2989dab1fee)
in a separate worktree. The old pinned builds and proof captures remain intact.
The {checkerupdatesource}`build wrapper <scripts/slurm_build_micromath_update.sbatch>`
compiles both arithmetic builders/provers and the independent kernel in SLURM,
using the provisioned C-header manifest and qualified workspace's GMP.
Build `362913` completed; `362870` had failed on missing `stdio.h` because the
first wrapper omitted those header paths. Dependent replay `362904` was
cancelled before it ran. Build flags, compiler, dependency hashes and binary
hashes are retained with the evidence.

The {checkerupdatesource}`replay driver <scripts/replay_arithmetic_checker_update.py>`
and {checkerupdatesource}`SLURM wrapper <scripts/slurm_replay_arithmetic_update.sbatch>`
replay exactly the previously audited five LIA and three LRA inputs. They check
build/revision identities and saved payload hashes, rebuild each challenge
with the updated builder, and pipe `--raw` output into the matching kernel.
Primo is not run again and the input/CNF/certificate bytes are unchanged. LIA
uses the already audited version-2 envelope copy, whose body matches the native
version-3 certificate. Prior Yices and independent witness/SAT checks are reused
through their recorded hashes; this is fresh checker evidence, not a new
independent audit of the producer.

Array `362914` gives each worker two CPUs, 12 GiB and 30 minutes. Challenge
construction retains a 30-second limit; proof and kernel share one overall
60-second live-pipeline deadline. Every external process has the previous
3 GiB address and 256 MiB file caps. Both zero exits, exact `challenge proven`
and no inconsistency marker are required. The worker exit code alone does not
establish that its cases passed.

All five LIA cases pass. Their elapsed challenge-plus-live-pipeline times are
7.60, 0.40, 1.70, 2.29 and 0.81 seconds for cases 011, 014, 015, 018 and 019.
These measurements exclude worker-level axiom construction and payload hashing;
they are not the author's complete-script measurements or a full-corpus result.
The LRA LassoRanker case also passes in 3.64 seconds; its source-conversion
counter falls from 175,076,611 to 56,857 kernel steps. The five- and six-clock
cases still reach the 60-second live-pipeline deadline. Thus the supplied LIA
response is reproduced on these inputs, while two LRA resource cases remain
outside that original budget. No wrong-answer inference follows from a timeout.

The {checkerupdatesource}`staged SLURM wrapper <scripts/slurm_capture_arithmetic_update.sbatch>`
selects the two remaining clock cases in separate workers. It retains two CPUs,
12 GiB, the child resource caps and 30-second challenge limit, and allows
300 seconds each for export and independent replay, with 16 GiB raw and 4 GiB
compressed proof bounds. Results go to
`results/arithmetic-update-capture-$SLURM_ARRAY_JOB_ID/case-N`.

The same driver can export selected LRA cases separately from kernel replay,
using `--capture-only`, repeated `--case-name` arguments and explicit capture
time/raw/gzip bounds. This mode is recorded separately from the live-pipeline
results. Complete captured proofs require their own successful decompressor and
kernel replay; an incomplete capture cannot count as verification. Its
reaped-child CPU counters exclude Python compression CPU, and peak child RSS
is the maximum observed across reaped children so far in that worker, rather
than a per-case allocation measurement.

The {download}`updated-checker ledger <../coverage/micromath-arithmetic-update.json>`
records both budget regimes and the unchanged input hashes. Follow-up array
`362921` completes both remaining clock exports and their independent kernel
replays under the staged bounds:

| Case | Previous raw GB | Updated raw GB | Updated gzip GB | Updated export seconds | Updated kernel seconds |
| --- | ---: | ---: | ---: | ---: | ---: |
| LassoRanker | 2.560 | 0.086 | 0.038 | 4.29 | 1.42 |
| Five clocks | 2.207 | 2.026 | 0.978 | 88.21 | 32.88 |
| Six clocks | 3.691 | 3.234 | 1.562 | 139.28 | 52.33 |

The LassoRanker capture is the extra diagnostic from `362914`; the clock
captures are from `362921`. GB means one billion bytes. All three complete gzip
files are accepted by the updated kernel, including successful decompressor
exits. These staged timings include compression/decompression and I/O and
should not be read as controlled live-pipeline speed measurements.

The clock costs remain dominated by arithmetic proof expansion: five-clock
arithmetic checking changes from 184,701,495 to 171,076,770 kernel steps;
six-clock changes from 310,207,234 to 273,514,122. Updated source conversion
uses only 9,313 and 13,426 steps respectively. This is residual resource
feedback for these two LRA inputs, not a contradiction of the author's five
successful LIA replays or a correctness failure. The original 60-second
budget still excludes these two updated-checker runs; larger staged bounds
establish their proof acceptance.

A {download}`54,742-byte updated clock packet <../../benchmarks/checker-regressions/lra-clock-update-20261107/updated-clock-feedback.tar.gz>`
contains both unchanged input/CNF/CORE triples, fresh diagnostics and progress,
live and staged summaries, payload hashes and
{download}`manual replay instructions <../../benchmarks/checker-regressions/lra-clock-update-20261107/README.md>`.
All 21 manifest-listed payload hashes were checked after packing. Full proofs
are omitted. New challenges and gzip proofs remain under
`$PRIMO_WORKDIR/results/arithmetic-checker-update-362914/LRA` and
`results/arithmetic-update-capture-362921/case-N`; old builds and captures remain
available. The packet rebuilds challenges with `cfd36f46` instead of reusing the
old checker's metadata snapshots. No native solver or certificate-format change
was needed for the five LIA fixes or the LassoRanker reduction. This targeted
eight-input check does not rerun the full 217-input LRA cohort.

### Larger arithmetic sample with the updated checker

Array `363005` evaluates 345 inputs with Micromath `cfd36f46`: the unchanged
217-file LRA cohort above, plus 128 LIA files. The
{download}`LIA selection <../../benchmarks/qf-lia-certificate-sweep-20261107.json>`
reserves all 24 earlier LIA pilot controls and adds 104 inputs from the recorded
fast UNSAT pool. It covers 20 families and source sizes of 4,097–3,439,267 bytes.
There are 32 files in each of the four size bands: 4–32 KiB, 32–128 KiB,
128–512 KiB and 512 KiB–8 MiB. The LRA selection retains its earlier 24 controls.
These are source-size bands, not bounds on CNF, certificate or proof size.

Selection `363004` finds 2,210 recorded-fast LIA UNSAT rows: 1,115 meet the
configured size/admission screen, while 1,095 fall outside the size interval.
The {arithmeticlargersource}`selector <scripts/select_arithmetic_certificate_sweep.py>`
now accepts `--logic QF_LRA|QF_LIA`, keeping QF_LRA as its default. Its shared
{arithmeticlargersource}`sampling routine <scripts/arithmetic_certificate_stress.py#L37>`
still reserves hash-matched controls, visits families round-robin within bands
and fills remaining capacity deterministically. The LIA count and family cap
are both 128. Historical UNSAT time is at most one second at `5efbd022` with
`--quiet`; these timings select candidates and do not measure this workflow.
Single-query/no-positive-arity admission remains a conservative lexical screen,
not a full check of the external checker's supported fragment.

The {arithmeticlargersource}`combined wrapper <scripts/slurm_arithmetic_certificate_sweep.sbatch>`
dispatches eight LRA shards and four LIA shards, at most four concurrent, with
two CPUs, 12 GiB and two hours per task. Before calling the shared stress
wrapper, each task checks its shard hash, count and logic and all six updated
checker binaries against the qualified build report `362913`. LRA uses the
unchanged frozen selection `362760`; LIA uses the four disjoint shards from
`363004`. Each task writes to
`results/arithmetic-certificate-sweep-363005/LOGIC/shard-N`.

Every selected input gets a fresh Yices check; only a successful `unsat` result
proceeds to native CORE export using the same qualified `a5123159` binary. Limits remain 15 seconds for Yices, 30 each
for export, statement conversion and challenge construction, and one overall
60-second proof/kernel deadline. External processes retain 3 GiB address and
256 MiB file caps. Raw proof steps stream directly to the kernel; no raw proof
file is written. Successful challenge dumps are hashed and removed, while
native certificates, exported statements and failed-stage artifacts remain.
Case counts determine coverage; a completed scheduler task does not imply
that all its proofs verified. The source-to-CNF transformation remains trusted.
This is an expanded sample, not full-corpus or default-profile qualification;
there is no new solver implementation or native C++ change.

The {download}`completed-sweep ledger <../coverage/arithmetic-certificate-sweep-cfd36f46.json>`
records all 345 outcomes, per-family and size-band counts, timing distributions,
per-case hashes, stage evidence and scheduler accounting:

| Logic | Selected | Kernel verified | Producer reconstruction limit | Proof deadline |
| --- | ---: | ---: | ---: | ---: |
| QF_LRA | 217 | 216 | 0 | 1 |
| QF_LIA | 128 | 124 | 4 | 0 |
| Total | 345 | 340 | 4 | 1 |

All 345 fresh Yices checks returned UNSAT. There were no reference disagreements,
wrong answers or checker errors. Of the verified cases, 148 LRA and 90 LIA
cases retain theory lemmas; 102 need none after preprocessing/CORE. Across the
341 completed native exports, CORE reduces 45,918 collected lemmas to 18,054.
All 24 LRA controls verify; 23 of the 24 LIA controls verify, with the previously
known `dillig/25-22.smt2` reconstruction limit remaining.

All 217 LRA native certificates and exported-CNF statements are byte-identical
to the earlier `362761` sweep. Under these limits, verification rises from
214 to 216 cases: LassoRanker and five-clock now pass, while six-clock still
hits the deadline. Five-clock takes 59.39 seconds for the live pipeline,
very near the 60-second cutoff; it timed out in the earlier targeted replay
at the same updated checker revision. That borderline outcome is timing
sensitive, not a robust speed/default-promotion claim. The six-clock payload
is also unchanged from its successful longer staged replay; the existing
{download}`updated clock packet <../../benchmarks/checker-regressions/lra-clock-update-20261107/updated-clock-feedback.tar.gz>`
still reproduces that resource case. LIA control gains also include the
previously resolved Z3-to-Yices reference gate; they must not all be attributed
to checker changes.

The four LIA cases stop in Primo, before statement conversion or checker
invocation. Each records main-solver UNSAT, completed CORE and exactly one
retained theory conflict. Reconstruction then reports
`LiaCertificateStatus::ResourceLimit` and writes no certificate:

| Input | Generated cuts | Pairs |
| --- | ---: | ---: |
| `prime-cone/prime_cone_unsat_16.smt2` | 7,269 | 4,782 |
| `prime-cone/prime_cone_unsat_17.smt2` | 6,961 | 4,833 |
| `dillig/25-22.smt2` | 5,224 | 5,186 |
| `dillig/25-31.smt2` | 4,740 | 4,681 |

The {arithmeticlargersource}`reconstruction limits <src/theory/arithmetic/lia_certificate.hpp#L28>`
are 8,192 cut steps, 4,096 active rows, 65,536 pairs and 1,024 digits per integer.
The current diagnostic does not identify which guard fired; the generic message
does not imply that the cut-step cap was exhausted. These are bounded
certificate-reconstruction limitations after CORE, not failures to refute the
original formulas or invalid proofs accepted by the checker. No limit or solver
implementation was changed during this experiment.

The {download}`20,528-byte producer packet <../../benchmarks/certificate-regressions/lia-reconstruction-20261107/lia-reconstruction-limits.tar.gz>`
preserves all four original inputs, fresh Yices/native logs, counters, hashes
and a bounded replay command. See its
{download}`guide <../../benchmarks/certificate-regressions/lia-reconstruction-20261107/README.md>`.
All 25 manifest-listed payload hashes were checked. Three producer cases are new
to this expanded sample; `dillig/25-22.smt2` reconfirms the known limitation.
Full benchmark artifacts remain in the per-shard result directories above.

### Full recorded-UNSAT arithmetic sweeps

The full scope is the completed default-profile UNSAT records in baseline
`5efbd022`, with current input hashes verified. Inventory `365547` finds 605
LRA and 3,244 LIA records, all present, from 15 and 25 families. Their historical
maximum solve times are 57.03 and 59.92 seconds. Source sizes reach 11,282,707
and 57,992,842 bytes. The
{arithmeticfullsource}`inventory wrapper <scripts/slurm_inventory_arithmetic_certificates.sbatch>`
reads benchmark provenance and file sizes in SLURM; it does not solve instances.

Selection `365548` removes the sample's one-second, source-size, family and
case-count caps. The {arithmeticfullsource}`selector <scripts/select_arithmetic_certificate_sweep.py>`
uses `--all-known-unsat --cases-per-shard 25`, calling `read_history` without a
timing cutoff and retaining all eligible records. Its existing conservative
single-query/no-positive-arity screen and hash deduplication remain; all 3,849
records pass, with no exclusions. This includes all 217 LRA and 128 LIA controls
from the preceding sample. The screening pass retains record metadata and uses
memory proportional to the largest individual source, rather than retaining
all formulas together.

The {download}`frozen selection archive <../../benchmarks/full-arithmetic-certificates-20261108/selection.tar.gz>`
contains both complete selections, 155 disjoint shard manifests, hashes and
workflow provenance. There are 25 LRA shards and 130 LIA shards, each containing
at most 25 cases. The partition is deterministic round-robin over the sorted
selection, so larger files are distributed among workers. A full sweep here
means this recorded solved-UNSAT set; it does not extend the certificate format
to SAT or incremental sessions.

The {arithmeticfullsource}`full wrapper <scripts/slurm_full_arithmetic_certificates.sbatch>`
runs as array `365550`, initially with eight concurrent tasks, two CPUs, 12 GiB
and four hours per task. At 19:17:32 UTC on 8 November, the scheduler throttle
was raised to 32 after idle CPU nodes were confirmed. The frozen inputs,
binaries, per-worker allocation and per-case limits stayed the same; this
campaign is coverage evidence, not a speed comparison across those allocations.
It uses the same qualified native `a5123159`, Micromath
`cfd36f46`, Yices 2.7.0 and LRAT-capable CaDiCaL as the preceding evaluation.
Every attempted case first requests a fresh Yices check; only a successful
UNSAT answer proceeds to native CORE export. Each worker verifies its frozen
shard and all six checker binaries against build `362913` before using the
shared stress workflow.

The full profile allows 60 seconds for Yices, 120 for native export, 60 each
for statement conversion and challenge construction, and one overall
120-second proof/kernel pipeline deadline. The external-process bounds remain
3 GiB address space and 256 MiB per file. The
{arithmeticfullsource}`shared wrapper <scripts/slurm_arithmetic_certificate_stress.sbatch>`
accepts these through `ARITHMETIC_REFERENCE_SECONDS`, `ARITHMETIC_EXPORT_SECONDS`,
`ARITHMETIC_CHALLENGE_SECONDS` and `ARITHMETIC_PROOF_SECONDS`; its old sample
defaults remain 15/30/30/60. These larger budgets cover historically slower
refutations and define a different profile from the 345-case sample. Control
outcomes therefore cannot be read as a direct checker-speed comparison.

Native reconstruction limits are unchanged. Theory lemmas are CORE-reduced
before witness reconstruction; raw Micromath steps stream to the independent
kernel, with both exits and exact acceptance required. Successful challenge
dumps are hashed then removed; failed-stage artifacts remain under
`results/full-arithmetic-certificates-365550/LOGIC/shard-N/run`. Source-to-CNF
preprocessing/clausification remains trusted.

After its run-directory and exit trap are initialized, a handled nonzero
worker exit creates an exclusive `STOP` record identifying the first failing
task and requests cancellation of only its own array. Queued workers also
check that marker before starting. Earlier environment/directory failures and
external termination are outside this guarantee. The guarded exits include
the stress driver's fatal wrong-answer/reference-disagreement/workflow-error
statuses and later initialization failures;
other recorded per-case outcomes, including resource limits and nonzero native
or checker process exits, remain soft outcomes. In particular, an unexpected
native abort is recorded as an export error and does not itself trip this
array's stop guard; the final analysis must identify it separately. The
{arithmeticfullsource}`orchestration tests <tests/python/test_full_arithmetic_sweep.py>`
exercise full-mode cap removal, bounded/disjoint shards, budget dispatch,
first-failure preservation, own-array cancellation and a failed binary check,
using synthetic inputs and a mock cancellation command. No native solver or
external checker implementation is changed by this workflow.

The {download}`full-sweep ledger <../coverage/full-arithmetic-certificates-20261108.json>`
records the protocol, frozen selection identities, job ids and complete case
census. The outcomes below distinguish reference, producer and checker limits
from successful proof acceptance; raw per-worker summaries preserve the stage
evidence.

### Full-sweep outcomes and failure boundaries

Array `365550` finished all 155 tasks and accounted for all 3,849 frozen inputs.
This completes the census of the recorded solved-UNSAT cohort; the resource and
reconstruction outcomes below remain gaps in certificate coverage.

| Outcome | QF_LRA (605) | QF_LIA (3,244) |
| --- | ---: | ---: |
| Kernel verified | 561 | 2,639 |
| Yices (60 s) deadline | 28 | 5 |
| Native export (120 s) deadline | 0 | 9 |
| Native file limit (`SIGXFSZ`) | 0 | 192 |
| Native allocation failure (`std::bad_alloc`) | 0 | 115 |
| Native API-state crash (`SIGABRT`) | 0 | 2 |
| Post-CORE reconstruction `ResourceLimit` | 0 | 88 |
| Post-CORE reconstruction `NotFound` | 0 | 10 |
| Post-CORE reconstruction interruption at 60 s | 0 | 1 |
| Our statement conversion (60 s) deadline | 0 | 90 |
| Micromath challenge (60 s) deadline | 0 | 3 |
| Micromath producer out of memory | 0 | 83 |
| Micromath `definition index allocation` failure | 0 | 3 |
| Overall proof/kernel (120 s) deadline | 16 | 4 |

All 3,816 successful fresh Yices runs answered UNSAT. The remaining 33
reference timeouts stopped before native export. No wrong answer, reference
disagreement or fatal workflow error was recorded. Every verified row has both
producer/kernel exit codes zero and the exact `challenge proven` response,
without `Inconsistency proven!`. Task exit zero alone is not proof acceptance.

The 217 LRA controls all verify, including the six-clock case that timed out in
the preceding 60-second profile. All 217 native-certificate and exported-CNF
hash pairs match that sample. The 128 LIA controls give 124 verified and four
incomplete reconstructions; all 124 completed certificate/statement pairs match
the preceding sample. The native
and checker binaries are unchanged between these two evaluations. The larger
budgets and changed concurrency prevent attributing the changed outcomes solely
to checker improvements or treating them as a speed comparison.

CORE reduces the completed LRA exports from 987,954 collected theory lemmas to
426,364 retained lemmas; completed LIA exports retain 182,895 of
589,929. It does not minimize the initial CNF. The small natural LIA
checker-memory replay below has **zero theory lemmas** but 1,401,073 CNF clauses;
its native `:sat.clauses` and `:export.cnf.clauses` counters agree. Further theory
lemma minimization cannot remove that initial-clause parsing cost.

The failures have different boundaries:

- Native file-limit exits are `SIGXFSZ` under the 256 MiB per-file cap;
  allocation failures report `std::bad_alloc` under the 3 GiB address-space cap.
  The exact output/spool and execution phase are not logged. Native export
  deadlines are also not localized to an internal phase.
- Two native SMPT `RwMutex` exports abort with `SIGABRT` and CaDiCaL's
  `disconnect_external_propagator` invalid-state diagnostic. These are producer
  crashes, not classified memory-limit exits. Their root cause is untriaged;
  the inputs and logs are preserved separately. They block performance/default
  promotion claims until resolved or precisely explained.
- All 99 reconstruction failures occur after main UNSAT and completed
  CORE reduction. 88 report `ResourceLimit`, whose individual guard
  is not logged; 10 finish the implemented bounded procedure without
  finding a cut proof (`NotFound`). One further row reports reconstruction
  interruption after 60.000224 seconds, consistent with the internal 60-second
  phase budget; the exact stop flag is not printed. These outcomes are distinct
  from a satisfiable answer. The `NotFound` cases include a 487-byte,
  three-variable prime-cone input with one retained lemma, making a small
  producer-side investigation target.
- Statement deadlines occur in our streaming Python CNF-to-SMT-LIB adapter.
  Bounded memory does not ensure adequate throughput on these large CNFs.
  Separate challenge-construction deadlines occur inside the Micromath builder,
  before a proof/kernel pipeline is started.
- The 83 Micromath error rows report producer out-of-memory diagnostics.
  A following kernel `invalid solution` message after producer failure is not
  evidence that a complete proof was rejected. The overall proof-pipeline
  deadlines do not establish which internal phase dominated. Three further
  producer rows report `Logic failure: definition index allocation`; its exact
  cause is untriaged, and the diagnostic alone does not establish which limit
  fired. The checker implementation was neither inspected nor changed.

The {download}`producer feedback packet <../../benchmarks/certificate-regressions/full-arithmetic-20261108/producer-feedback.tar.gz>`
is about 80 KiB and preserves four natural examples: reconstruction `NotFound`,
reconstruction `ResourceLimit`, native file limit and native allocation failure.
The separate {download}`native crash packet <../../benchmarks/certificate-regressions/full-arithmetic-20261108/native-api-crashes.tar.gz>`
preserves two SMPT inputs and exact abort diagnostics without inferring that
an allocation failure caused the invalid backend state.
The {download}`checker feedback packet <../../benchmarks/checker-regressions/full-arithmetic-20261108/checker-feedback.tar.gz>`
is 13.8 MB compressed and contains the seven-clock timeout and a natural LIA
producer-OOM case. The latter needs a 100,876,052-byte certificate and a 116,007,079-byte exported-CNF statement
before compression. Its version-2 LIA compatibility copy changes only the
native header byte, preserving the body. The packets contain original inputs,
case receipts, diagnostics and replay instructions; they contain no raw proof.
The checker replay script was syntax checked only during packaging, with no
additional solver/checker rerun. No empty-native-CNF transport experiment was
performed in this campaign.

The separate {download}`wisa5 allocation-diagnostic packet <../../benchmarks/checker-regressions/full-arithmetic-20261108/lia-definition-allocation.tar.gz>`
is only 356 KB compressed. It preserves an 18,892,343-byte certificate and a
270,318-byte statement for the distinct `definition index allocation` producer
failure, with 10,598 retained lemmas and no assertion about its root cause.
Its replay script was also syntax checked without an additional checker run.

The {download}`challenge-deadline packet <../../benchmarks/checker-regressions/full-arithmetic-20261108/lia-challenge-deadline.tar.gz>`
contains only the prepared 38,750,749-byte `wireRouting/p15` statement, stage
receipt and a builder-only replay. It omits original/native bodies because
challenge construction does not read them. Its compressed size is 3.95 MB; the replay's success means challenge construction completed, with no proof
or kernel acceptance claimed. Packaging performed no extra checker run.

A single {download}`compact checker handoff <../../benchmarks/checker-regressions/micromath-handoff-20261109/micromath-checker-feedback.tar.gz>`
combines these four checker examples into one 18.1 MB archive with a portable
`replay.sh` selector. They are the smallest natural cases in each observed
checker-failure category by required uncompressed input bytes: statement plus
proof input, or statement alone for challenge construction. No input reduction
or new checker run was performed. The
{download}`handoff instructions <../../benchmarks/checker-regressions/micromath-handoff-20261109/README.md>`
explain extraction, tool paths, original budgets and the exported-CNF boundary.
Its complete index lists all 109 checker failures/timeouts (83 producer OOM,
three definition-index diagnostics, 20 proof-pipeline deadlines and three
challenge deadlines); only four representative payloads are embedded. Native
crashes, native export/reconstruction failures and our statement-conversion
failures are excluded. Start with `bash replay.sh wisa5 /fresh/output` after
unpacking and setting `MICROMATH` and `CADICAL` to the built-tool paths.

The {download}`full results ledger <../coverage/full-arithmetic-certificates-20261108.json>`
contains every case outcome, family totals, stage timings, control transitions,
payload identities and scheduler accounting. The
{download}`raw metadata archive <../../benchmarks/full-arithmetic-certificates-20261108/results-metadata.tar.gz>`
preserves the worker summaries, stage logs and receipts without formula,
certificate or raw-proof bodies. Large native certificates and failed-stage
artifacts remain in the recorded SLURM result directory. The eight workflow
checks, lint and documentation checks qualify the workflow changes; native
C++/active C++ tests remain unchanged from the qualified `a5123159` build, so
this sweep does not claim a new native suite, sanitizer or fuzz gate.

### Paired checker update, 10 November

Micromath `62353d2da3035fdc544c374156bd616b44fb8640`, fetched from upstream
`main`, recovers all four compact-handoff representatives at their tested
stages. This means **three kernel-verified proofs and one successful challenge
construction**, not four UNSAT proofs. The old `cfd36f46` build reproduces all
four failures in fresh controls; the comparison does not rely only on the
historical sweep timings.

| Representative | Fresh old checker | New checker |
| --- | --- | --- |
| `wisa5` | Definition-index allocation error, 71.75 s | Kernel verified, 27.87 s |
| `clock7` | Proof-pipeline timeout, 120.01 s | Kernel verified, 53.86 s |
| `lia-oom` | Producer out of memory, 6.13 s | Kernel verified, 16.16 s |
| `challenge-p15` | Challenge-builder timeout, 60.27 s | Challenge built, 24.06 s |

Times in the first three rows measure the live proof-producer/kernel stage,
after successful challenge construction. The last row measures only challenge
construction, with no proof producer or kernel invoked. Each is a single
observation; a failure's completion time is not comparable to a successful
proof as a general speedup ratio. The large-CNF case also takes 25.81 seconds
to build its new challenge, recorded separately from its 16.16-second proof.

Build `382225` compiles the new revision in an isolated worktree, using the
same GCC 11.4.1 compiler and qualified header/GMP dependency hashes as the old
build. Paired array `382226` has eight tasks, old/new for each example, with two
concurrent tasks, two CPUs, 12 GiB and ten minutes per task. All eight finish
with scheduler exit `0:0`; the per-case status, not task exit alone, determines
acceptance. The raw reports confirm that all eight replays use the same SLURM
node. The old worktree and binaries are retained unchanged.

The {checkerhandoffreplaysource}`replay helper <scripts/replay_checker_handoff.py>`
checks the worktree revision, six qualified checker-binary hashes, the original
CaDiCaL hash and the saved handoff archive/payload hashes. It copies only the
selected inputs in bounded blocks, without rebuilding native certificates or
regenerating statements. Atomic JSON checkpoints retain each finished stage.
The {checkerhandoffreplaysource}`SLURM wrapper <scripts/slurm_replay_checker_handoff.sbatch>`
assigns even tasks to the old build, odd tasks to the new one, and selects the
case by integer division of the task index by two. Both profiles use the same
30-second axiom, 60-second challenge and overall 120-second proof/kernel
budgets, with 3 GiB per-process address space and 256 MiB per-file limits.
Raw proof steps stream directly to the independent kernel; no raw proof file
is written. Proof acceptance requires both pipeline exits zero, exact
`challenge proven`, and no inconsistency marker. The builder-only case has its
own `challenge_built` status and cannot count as kernel verification.

The {download}`paired comparison ledger <../coverage/micromath-checker-update-20261110.json>`
records both fresh profiles, identical input hashes, stage timings, tool hashes,
compiler/dependency provenance and scheduler accounting. The
{download}`paired diagnostic archive <../../benchmarks/checker-regressions/micromath-update-20261110/paired-replay-results.tar.gz>`
contains normalized summaries and stage logs, omitting installation paths and
private host names. Original raw artifacts remain in
`results/checker-handoff-update-382226`. The
{checkerhandoffreplaysource}`five pure workflow tests <tests/python/test_checker_handoff_replay.py>`
exercise acceptance gates, timeout precedence and input-integrity rejection;
they execute neither solver nor checker.

This is a controlled improvement on four examples selected before the update.
It does not recheck the other 105 indexed failures, establish new full-corpus
coverage, or qualify a default performance profile. The existing native
producer failures are unaffected by this evaluation. The verified statement
remains the exported CNF: original-input-to-CNF conversion is trusted, and no
native solver run or checker-implementation inspection was performed here.

### Why lowering was already sufficient for the CNF boundary

The existing {liaexportsource}`lowering pass <src/smt/preprocessing.cpp#L4493>`
replaces a non-Boolean `(ite c t e)` with a fresh `u` and assertions
`c => u=t` and `not c => u=e`. These enter the ordinary preparation/clausification
path. The {liaexportsource}`collector <src/smt/cnf_export.cpp#L125>` captures
initial SAT clauses, including the surviving lowered constraints. It has not
lost those clauses at serialization. Later preprocessing may rewrite or remove
definitions while preserving the prepared goal; this route does not verify
those transformations.

Array `362720` instead tested original-source verification. Micromath's pinned
arithmetic route ignores the producer CNF and expects conflict variables to be
source variables. Its 12 unknown-variable LRA failures and all three LIA sort
mismatches pointed at `$primo_ite_0` in the exported signature. The declared
sorts were Real and Int respectively; this is not evidence that an original
source variable changed sort. Those results measure a boundary mismatch, not
failure to lower the ITE into the exported CNF.

The previously checked `11-ite` starter had no fresh ITE variables in its
certificate: its source equality was distributed to guarded equalities using
an existing source variable. New
{download}`LRA <../examples/qf-lra-shared-ite.smt2>` and
{download}`LIA <../examples/qf-lia-shared-ite.smt2>` regressions share a nested
arithmetic ITE between two inconsistent comparisons. Qualification requires
at least two generated ITE declarations and a retained conflict using an
auxiliary; a simplified Boolean-only refutation cannot satisfy that check.

Inspection of `CAV_2009_benchmarks/smt/15-vars/problem__016.smt2` also found all
30 source inequalities mathematically preserved in its 30 CNF units and all
16 conflict premises equal to source inequalities after affine normalization.
None matched the let-expanded source syntactically, because Primo removed
zero products and simplified multiplication by one. This narrows that case to
an expression-connection problem in the stronger route; it does not diagnose
all 14 missing-refutation cases. Original-source verification needs justified
normalization bridges and auxiliary definitions. Adding more unrelated theory
lemmas or repeating ITE lowering does not supply those connections.

## Micromath verification from the original source

Micromath revision `5f22700410c0e37044c03557dbd21e009303b7ba` provides
[`build-lra-challenge` and `prove-lra`](https://git.olsak.net/mirek/Micromath/src/commit/5f22700410c0e37044c03557dbd21e009303b7ba/smt/LRA.md).
The builder reads the original SMT-LIB. The prover uses our version-2 weighted
conflicts and independently derives its Boolean clauses; it ignores the producer's
CNF. Its proof is checked by a separate `check-solution` process. The statement
translation into Micromath remains trusted, with rational values representing
this finite rational-coefficient linear-real fragment. This does not verify
Primo's preprocessing transformation itself.

After building the upstream tools as documented there, run from Micromath's root:

```bash
set -o pipefail
bundle=/path/to/primo/benchmarks/checker-regressions/lra-starter-20261104
mkdir -p build/primo-lra
./build-axioms --fragment finitary build/primo-lra/axioms.umd
./smt/build-lra-challenge build/primo-lra/axioms.umd \
  "$bundle/inputs/05-rational-weight.smt2" build/primo-lra/challenge.umd
./smt/prove-lra build/primo-lra/challenge.umd \
  "$bundle/certificates/05-rational-weight.sexp" - --raw --cadical /path/to/cadical | \
  ./check-solution build/primo-lra/challenge.umd -
```

Both processes must succeed and the checker must print `challenge proven`.
`--raw` streams executed proof steps. A saved proof file is unnecessary for
this pipeline.

SLURM job `357143` checked all 12 original starter problems and their unedited
certificates. Three further positive runs checked zero weights, removal of the
producer CNF, and saved-theory reuse. Broken cancellation, lost strictness and
a satisfiable replacement source were rejected. In total, 15 runs reached
`challenge proven` and three controls were rejected, with no timeout. Every
proof producer and kernel process had a 3 GiB address-space limit; each stage
had a 60-second deadline. The
{micromathlrasource}`replay script <scripts/replay_micromath_lra.py>` and
{micromathlrasource}`SLURM wrapper <scripts/slurm_micromath_lra_check.sbatch>`
implement this bounded workflow. The
{download}`qualification ledger <../coverage/micromath-lra.json>` records exact
hashes, commands, initial infrastructure failures and artifacts.

This qualifies the small version-2 LRA set, not the real benchmark corpus or
arbitrary preprocessing output. Conflict variables must be original source
Real variables; surviving exporter auxiliaries or unsupported source syntax
can prevent a refutation. The [integer cut route](qf-lia-certificates.md#micromath-integer-proofs-and-envelope-compatibility)
has its own tools, envelope requirement and separate qualification.


## Real-source arithmetic stress pilot

SLURM array `362720` tested 24 real LRA and 24 real LIA sources on 6 November
2026, using the native binary `a5123159` and the pinned Micromath revisions
above and in the [LIA compatibility section](qf-lia-certificates.md#micromath-integer-proofs-and-envelope-compatibility).
Each source had a hash-matched historical native UNSAT run of at most one second
at `5efbd022`. Both historical runs and this pilot used default preprocessing;
this pilot added CORE export. Inputs were unchanged single-query files selected
from 4 to 512 KiB, with at most three per family. The target was eight per size
band (4–32, 32–128, 128–512 KiB); family availability left LRA at 14/9/1 and LIA
at 8/8/8. This is a bounded pilot, not full arithmetic coverage.

| Outcome | LRA | LIA |
| --- | ---: | ---: |
| Original-source kernel proof verified | 3 | 3 |
| Supplied conflicts do not refute the checker abstraction | 6 | 8 |
| Proof producer error | 12 | 5 |
| Source challenge builder error | 3 | 6 |
| Native proof reconstruction incomplete | 0 | 1 |
| Reference timeout; export skipped | 0 | 1 |

One verified case per logic needed arithmetic: `sal/tgc/tgc_io-safe-9.smt2`
used 36 LRA conflicts; `20220307-SMPT/Eratosthenes-PT-500/RC-02.smt2` used one
LIA conflict and one cut. The other four verified sources were propositionally
refuted with zero theory conflicts. Acceptance required both producer and
kernel exits zero, `challenge proven`, and no inconsistency marker.

CORE removed 1,981 of 4,350 collected LRA lemmas and 666 of 1,154 LIA lemmas
across native export attempts. These counts include the LIA attempt whose
subsequent reconstruction failed. CORE is an UNSAT-core reduction, not a
minimality guarantee. Raw proofs were piped directly into the kernel. No
checker timeout, memory kill or file-size cap was observed. Each task finished
in about 80 seconds; this is certificate-workflow evidence, not a solver-speed
comparison.

All 12 LRA producer errors reported unknown source Real variable
`$primo_ite_0`. Three LIA errors reported source/certificate sort mismatches;
two reported evaluation needing a closed Boolean term. Source builders rejected
five files at `define-fun` and four at nesting depth 512. Fourteen further cases
reported `s SATISFIABLE` for the checker's Boolean abstraction with the supplied
conflicts. That diagnostic does not assert that the original arithmetic problem
is satisfiable. The exact missing source-level refutation has not been diagnosed.
For `dillig/25-22.smt2`, Primo's main solver proved UNSAT but bounded integer
witness reconstruction reached its limit, returned `unknown` and wrote no
certificate. The remaining reference timeout was `pidgeons/pigeon-hole-19.smt2`.

The {download}`evidence ledger <../coverage/arithmetic-proof-stress.json>` records
all 48 input hashes, family results, exact binary/workflow hashes, stage outcomes,
limits and timings. Full artifacts are mirrored locally at
`results/arithmetic-proof-stress-362720` and retained in the prepared remote
workspace under the same results directory. Successful challenge dumps were
hashed then removed; failed challenges, certificates and logs were retained.
No raw proof file was created. A
{download}`201 KiB replay archive <../../benchmarks/checker-regressions/arithmetic-stress-20261106/arithmetic-stress.tar.gz>`
contains seven representative failures, original sources, native certificates
where produced, explicitly labelled LIA header copies, logs, hashes and commands.
These are small examples within their categories, not delta-minimized instances.
The checker implementation was neither inspected nor modified for this pilot.

To repeat this historical original-source pilot, use the frozen workflow
`d99bc152` below. The [current default workflow](#exported-cnf-verification)
checks the exported CNF instead. Use the historical
{arithmeticstresssource}`SLURM wrapper <scripts/slurm_arithmetic_certificate_stress.sbatch>`
with an already qualified native workspace and pinned external builds. Set the
workspace, historical timing root, both corpus roots, both checker directories
and revisions, LRAT-capable CaDiCaL path, and frozen workflow directory through
the environment variables listed at the top of that wrapper. Run the canonical
workspace preflight immediately before submission. Each array task requests
two CPUs, 12 GiB and 90 minutes; per-process address space is 3 GiB and maximum
file size is 256 MiB. Deadlines are 15 seconds for Z3, 30 for challenge/export,
and 60 for the proof/kernel pipeline. The
{arithmeticstresssource}`runner <scripts/arithmetic_certificate_stress.py>`
checks Z3 first, builds the challenge from the original source, exports CORE,
and streams the proof. For LIA, only byte 28 of a separate certificate copy
changes from version 3 to version 2. A task's successful exit or `FINISHED` file
means orchestration finished; proof coverage is the `verified` count in its
`run/summary.json`. No preprocessing-disabled retry was made. Further expansion
should resolve these interface gaps before increasing the workload.
