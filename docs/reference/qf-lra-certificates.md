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
