# QF_LIA integer cut certificates, version 3

**Status: experimental bounded implementation; bounded SLURM checks completed with native-search limits reported below.**
`--export-cnf FILE` accepts a single QF_LIA query with the same command/session
restrictions as [UF export](qf-uf-conflicts.md). Existing UF/version-1 and
[LRA/version-2](qf-lra-certificates.md) records are unchanged. Version 3 lists
named signed premises and a checked integer cut derivation in each
`conflict-lia` record. The exported CNF is the starting problem; source-to-CNF
conversion remains unproved.

```bash
./build/primo --quiet --preprocessing-mode=off --export-cnf result.sexp \
  --export-cnf-reduction=core docs/examples/qf-lia-certificate-proposal.smt2
python3 scripts/check_lia_certificate.py result.sexp --sat-solver /path/to/cadical
```

The {download}`input <../examples/qf-lia-certificate-proposal.smt2>` is the
two-variable example in the [design note](../leads/lia-conflict-certificates.md).
Its {download}`hand-written certificate <../examples/qf-lia-certificate-proposal.sexp>`
is a valid example of the schema, not a byte-for-byte expected producer output.
The {download}`native certificate <../examples/qf-lia-certificate-native.sexp>`
uses `pN`/`sN` names and derives the same contradiction in two cuts; its
{download}`provenance <../examples/qf-lia-certificate-native.json>` records the
command, source and independent check. The normal
preparation path lowers equalities and extended integer terms; disabling
optional preprocessing makes the example's arithmetic conflicts observable.

## Integer rule and supported fragment

Declarations are `Int` or Boolean constants. Arithmetic premises are signed
affine comparisons over integer coefficients/constants. Negation and strictness
are normalized internally: $P<b$ becomes $P\le b-1$, whereas
$\neg(P\le b)$ becomes $-P\le-b-1$. The original signed literal is printed
unchanged in the premise list. Integer-specific `div`/`mod`/`abs` and `ite`
must pass the existing native lowering before collection. Non-affine surviving
terms, Real variables, uninterpreted functions/sorts, mixed logics, quantifiers,
sort erasure and incremental queries are outside the initial export fragment.

```lisp
(conflict-lia
  (premises
    (a (<= (+ (* 3 x) (* 2 y)) 4))
    (b (<= 1 y))
    (c (<= 1 (* 2 x))))
  (proof
    (cut upper 3 (a 1) (b 2))
    (cut lower 2 (c 1))
    (cut impossible 1 (upper 1) (lower 1))
    (contradiction impossible)))
```

Labels are unique within one record, with references to premises or earlier
steps only. They never name SAT variables or source terms. A cut forms a
nonnegative integer weighted sum $A\cdot x\le B$. Its positive integer
divisor $d$ must divide every variable coefficient in $A$; the checker computes
$ (A/d)\cdot x\le\lfloor B/d\rfloor $. Divisor 1 gives ordinary summation.
Negative bounds use mathematical floor, not truncation. All numbers in the
serialized arithmetic are integers. `contradiction` must be the final
instruction and reference a zero left-hand side with a negative bound.

Successful checking justifies the clause of complements of **all** original
premise literals. Unused premises, zero weights and repeated references are
allowed. Derived cuts are not additional assumptions and do not enter the SAT
clause. Each conflict is checked independently of other CNF clauses or records.

## Reconstruction, ownership and limits

The {liaexportsource}`SMT collector <src/smt/cnf_export.hpp>` observes the real
clausifier-to-SAT boundary through
{liaexportsource}`solve_component_recipe <src/smt/component_solver.cpp#L71>` and
complete {liaexportsource}`component reason/external-clause streams <src/smt/component_dispatcher.cpp#L244>`, including the integer completion
conflicts learned after full-model checks. The single unguided LIA recipe
accepts the optional collector; other component recipes retain their default
behavior. The dispatcher observer receives only SAT clauses and is empty in
ordinary solving. No coefficients or integer proof trace are stored in main
search. Atom bindings seal after initial preparation; a later clausifier
emission fails export rather than silently using an incomplete mapping.

As for LRA, {liaexportsource}`raw numeric theory clauses <src/smt/numeric_clause_spool.hpp>`
are spooled and CORE/MUS selects complete clauses before
{liaexportsource}`SMT reconstruction <src/smt/cnf_export.cpp#L326>`. `none` uses sequential numeric records with no
offset index. Proof text is staged on disk; the final output opens only after
all retained conflicts have completed. A cancelled or incomplete reconstruction
leaves an existing output unchanged. Final-publication I/O failure may still
leave a partial file, as in the other formats.

The {liaexportsource}`arithmetic API <src/theory/arithmetic/lia_certificate.hpp>`
owns sparse integer-valued premises, cuts and result counters. It uses
integer-valued `util::Rational` objects internally to reuse arbitrary-precision
arithmetic and overflow handling; no rational numeral is serialized. Variable
IDs are opaque to this layer. Per-lemma projection rows and proof state are
released before the next retained lemma; the collector still borrows the
query term manager and retains the CNFs needed by subset reduction.

The {liaexportsource}`reconstructor <src/theory/arithmetic/lia_certificate.cpp#L91>`
first tightens each premise by its coefficient GCD. It tries the existing
private LRA witness reconstruction on those tightened rows, clearing all
weight denominators to obtain an integer sum. This handles rational
contradictions without projecting variables. Otherwise it tries bounded
Fourier–Motzkin real-shadow projections, preferring a variable with unit
coefficients on one whole side (an exact projection), then a small pair count.
Each opposite-sign pair is combined with positive integer weights and GCD
tightened. Duplicate left-hand sides retain the strongest active bound.
Only ancestors of the final contradiction are serialized; all selected original
premises remain. Arithmetic replay checks the final derivation before printing.

This is a bounded refutation baseline, **not** the complete Omega algorithm.
Dark shadows, splinters and scoped branch replay are absent. A real shadow can
have integer points even when the original system has none, so no contradiction
found means **inconclusive**, never satisfiable. Pugh's §2.3.3 example is an
explicit regression for this limitation. No full QF_LIA coverage is claimed.

Default per-lemma limits are 8,192 generated cuts, 4,096 active rows, 65,536
generated bound pairs and 1,024 decimal digits per integer. Original premises
also count against the row limit. A separate 60-second cooperative budget
covers the whole reconstruction phase. Polls occur during setup, projection,
replay and between private LP pivots; individual large exact-number operations
and formatting are not hard real-time bounded. External process limits remain
necessary. Pair generation can grow quadratically per projection and
exponentially overall; this baseline makes no performance claim.

Unavailable proofs return export `unknown`, preserve `:export.main-unsat=1`
when main solving finished UNSAT, and publish no artifact. Explicit limits
use the resource-limit reason; interruption uses cancellation/the phase limit.
Malformed/non-integer premises or an invalid reconstructed proof are export
errors, not legitimate refutations.

Counters include selected/attempted/finished lemmas, completion and phase time,
numeric/text spool bytes, generated/pruned cuts, projections/exact projections,
bound pairs, and private linear checks/pivots. For LIA,
`:export.reconstruction.entries` counts listed premise positions. A completed
CORE/MUS selection alone does not establish proof validity.

## Independent checks and qualification

The {liaexportsource}`Python checker <scripts/check_lia_certificate.py#L97>` uses
Python integer arithmetic, parses the serialized polynomials independently,
checks local reference scope and replays each cut including exact divisibility
and negative floor. It shares tokenization with the LRA checker, but no producer
matrix, tableau or reconstruction result. It rejects fractional proof numbers
and Real declarations. Optional external CaDiCaL checking confirms propositional
UNSAT; this trusts that SAT solver and is not a propositional proof-kernel check.
Without `--sat-solver`, only syntax and arithmetic are checked. The diagnostic
checker reads the whole artifact in RAM, unlike the producer's text spools.
Micromath now implements the integer cut rule; the
[external route below](#micromath-integer-proofs-and-envelope-compatibility)
documents its current envelope compatibility requirement.

Native unit coverage includes GCD/negative-bound tightening, the linear-witness
fast path, an example needing the exact-projection preference, proof pruning,
invalid cuts, duplicate coefficients, limit/cancellation paths and an incomplete
integer refutation preserving the output sentinel. Python mutation tests cover
divisibility, sign, type, scope, zero weights and false contradictions.
SLURM jobs `357089`/`357091` passed all 50 native tests, 321 Python tests
(with two skips), 50 ASan tests, 1,008 LRA export cases and 1,000 UF export cases.
The fresh LIA run completed 1,012 inputs: 1,006 passes (201 SAT and 805
independently checked UNSAT certificates), five native branch-node limits
confirmed on the previous qualified binary, and one 15-second timeout also
reproduced without export on both binaries. The strict LIA wrapper therefore
exited nonzero on that timeout; remaining regressions and ASan ran separately.
Limits are excluded from passes, with zero wrong answers or invalid certificates.

Job `357088` independently checked all eight selected real inputs, drawn from
recorded native UNSAT runs of at most one second. Seven outputs contain
arithmetic conflicts; together the outputs have 60 conflict records and 790
premise entries. This is a bounded sample, not full QF_LIA coverage. The
{download}`qualification ledger <../coverage/lia-certificate-export.json>` records
seeds, exact workflows, binary hashes, historical input hashes, job outcomes,
resource limits, timeout reproduction and artifact paths.

## Micromath integer proofs and envelope compatibility

The stress workflow defaults to [exported-CNF verification](qf-lra-certificates.md#exported-cnf-verification).
The original-source procedure below remains an explicit diagnostic mode. Both
use the envelope compatibility described here; native integer artifacts remain
version 3.

Micromath revision `8f9cff132140462b141183ddb9cb88a9beb80a4f` provides
[`build-lia-challenge` and `prove-lia`](https://git.olsak.net/mirek/Micromath/src/commit/8f9cff132140462b141183ddb9cb88a9beb80a4f/smt/LIA.md).
It implements named integer cuts, including exact coefficient division and
negative floor rounding, and checks the generated proof with its ordinary kernel.
The statement is built from the original SMT-LIB; the producer CNF is ignored.
SMT-LIB-to-Micromath statement translation remains trusted.

At this revision the envelope must say `(version 2)`. Our native `(version 3)`
file is rejected at that header, before arithmetic checking. A compatibility
copy changes only that single byte: `QF_LIA`, `Int` sorts, premises, coefficients,
divisors and proof steps remain unchanged. The native artifact remains version 3.
Bare `conflict-lia` records are also accepted. Micromath requires a nonzero
weight in each cut; the native producer already meets that condition. Zero
references in an otherwise nonzero sum are accepted. This is an envelope
compatibility issue, not a missing integer-cut rule.

From Micromath's root, after building its documented LIA targets:

```bash
set -o pipefail
examples=/path/to/primo/docs/examples
mkdir -p build/primo-lia
./build-axioms --fragment finitary build/primo-lia/axioms.umd
./smt/build-lia-challenge build/primo-lia/axioms.umd \
  "$examples/qf-lia-certificate-proposal.smt2" build/primo-lia/challenge.umd
# Native printer output puts its version header on line 2.
sed '2s/(version 3)/(version 2)/' "$examples/qf-lia-certificate-native.sexp" \
  > build/primo-lia/micromath-v2.sexp
./smt/prove-lia build/primo-lia/challenge.umd build/primo-lia/micromath-v2.sexp \
  - --raw --cadical /path/to/cadical | \
  ./check-solution build/primo-lia/challenge.umd -
```

Both processes must exit zero and the kernel must print `challenge proven`.
The byte-for-byte {download}`native artifact <../examples/qf-lia-certificate-native.sexp>`
needs the explicit header copy shown above; it is not directly accepted by this
Micromath revision. `--raw` streams executed steps, so a saved proof file is
unnecessary for the pipeline.

SLURM job `357147` verified six runs of the same original integer problem:
the native and hand-written proofs with version-2 envelopes, an empty producer
CNF, a zero-weight reference, a bare conflict, and saved-theory reuse. Five
negative controls were rejected: an inexact divisor, forward reference, negative
weight, invalid contradiction reference, and satisfiable replacement source.
The native version-3 envelope rejection was checked separately. No producer
payload or native solver code was changed. Job `357148` also repeated all 18
LRA checks successfully after sharing the replay helper.

The {micromathliasource}`LIA replay workflow <scripts/replay_micromath_lia.py>`
uses the {micromathliasource}`shared replay helper <scripts/replay_micromath_lra.py#L56>`
and a {micromathliasource}`SLURM wrapper <scripts/slurm_micromath_lia_check.sbatch>`.
These are small compatibility checks, not real-corpus coverage. The
{download}`small replay archive <../../benchmarks/checker-regressions/lia-envelope-20261106/lia-envelope.tar.gz>`
contains the original source, native and adapted certificates, a guide and
hashes. The {download}`qualification ledger <../coverage/micromath-lia.json>` records the
one-byte copies, hashes, commands, limits and logs. Source `div`, `mod`, `/`,
decimal literals, quantifiers, functions and incremental queries are outside
the documented Micromath LIA fragment. Existing Primo lowering of such terms
does not extend the checker's original-source parser.

The [Yices reference and checker-feedback audit](qf-lra-certificates.md#yices-reference-and-independently-validated-checker-feedback)
validated the five saved checker-failure inputs and exported a small replay
for the freshly reproduced Boolean-evaluation error. The remaining native
reconstruction limit is recorded separately as a producer limitation.

The [checker-update replays](qf-lra-certificates.md#replaying-the-authors-checker-update)
confirm that all five saved LIA feedback cases pass at Micromath `cfd36f46`,
using the unchanged audited version-2 copies and original resource limits.
This addresses those checker cases; the separate native reconstruction limit
remains a producer limitation.

The [larger updated-checker sample](qf-lra-certificates.md#larger-arithmetic-sample-with-the-updated-checker)
extends LIA evaluation to 128 selected inputs, including those 24 pilot controls,
and evaluates them alongside the frozen 217-case LRA cohort.

The [full recorded-UNSAT sweeps](qf-lra-certificates.md#full-recorded-unsat-arithmetic-sweeps)
remove sampling caps and cover the 605 LRA and 3,244 LIA recorded UNSAT inputs,
using a separately documented larger per-case budget profile.

The [real-source arithmetic stress pilot](qf-lra-certificates.md#real-source-arithmetic-stress-pilot)
adds 24 real LIA sources: three kernel proofs, including one using an integer
cut, with the remaining outcomes and small replay files recorded separately.

## Sources and implementation status

Pugh, expanded *Omega Test*, §§2.1, 2.3.1 and the exact-projection criterion in
§2.3.2 {cite:p}`pugh1992omega`, grounds the implemented integer tightening,
real-shadow elimination and variable preference. The
[design note](../leads/lia-conflict-certificates.md) maps the implemented subset
and proof obligations. The ordinary map implementation does not implement
Pugh's optimized constraint-key machinery or complete integer procedure.
The private linear-witness fast path reuses the implemented
[LRA construction](qf-lra-certificates.md). Source-to-CNF proof, complete integer
proof reconstruction and proof-language translation remain outside this work.
