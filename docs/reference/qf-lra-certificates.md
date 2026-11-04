# QF_LRA weighted conflict certificates, version 2

**Status: experimental implementation; SLURM qualification pending.**
`--export-cnf FILE` now accepts one QF_LRA query using the same single-query
command/session restrictions as [QF_UF export](qf-uf-conflicts.md). QF_UF keeps
its version-1 format; QF_LRA writes version 2. This certifies the prepared CNF
as a starting problem, without a proof connecting it to the original SMT script.
The [design note](../leads/lra-conflict-certificates.md) gives the mathematical
contract, reconstruction argument and implementation checklist.

```bash
./build/primo --quiet --export-cnf result.sexp --export-cnf-reduction=core input.smt2
python3 scripts/check_lra_certificate.py result.sexp --sat-solver contrib/local/bin/cadical
```

CORE and MUS select complete theory clauses before any coefficients are
reconstructed. `none` reconstructs every collected lemma; it does not retain
formatted text or a vector of all numeric lemmas in RAM. Empty selection needs
no reconstruction. SAT or inconclusive results write no artifact and leave
an existing output untouched. Reconstruction failure is not a SAT result.

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

The generic `smt::CnfConflictCollector` in `src/smt/cnf_export.*` replaces the
formerly UF-specific collector. Both Boolean/UF and LRA leaves bind it to the
actual clausifier observer. LRA collection covers complete external conflicts,
propagation reasons when their terminating zero is delivered, and lazy
fallback conflicts. No weights are stored by the normal LRA search.

The collector borrows the query's term manager, owns atom-ID/polarity bindings,
and keeps numeric CNFs only when subset reduction needs them. Its
`NumericClauseSpool` owns an anonymous C `tmpfile`, native-binary literal records
and, for reduction modes, record offsets. `none` uses sequential reading with
no offset index. These are private query-local records, not a portable format.
This numeric temporary file uses the C library's temporary-file location; the
existing text spool separately honors the repository's temporary-directory
mechanism. All temporary files close when their query owner is destroyed.

For each retained lemma, the collector normalizes only that lemma's facts and
passes sparse exact coefficients to `theory::arithmetic::reconstruct_lra_certificate`
in `src/theory/arithmetic/lra_certificate.*`. A fresh Bland tableau solves the
non-strict multiplier problem below. Here the rows of $M$ are normalized
premise coefficient vectors, $b$ contains their right-hand sides, $s_i$ is one
for a strict premise and zero otherwise, and $\lambda_i$ is that premise's weight.

$$
\lambda\ge0,\quad M^T\lambda=0,\quad b^T\lambda\le0,
\quad(s-b)^T\lambda=1.
$$

Its model supplies ordinary rational weights. A separate exact accumulator
checks the candidate before text is staged. The temporary arithmetic state is
released between lemmas. The tableau accepts an optional cancellation callback
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
Micromath/Ethos LRA translation is not implemented.

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
SLURM reference/witness/propositional validation driver; native, sanitizer,
fresh fuzzing and real-input qualification are still pending.

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
