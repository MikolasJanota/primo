# Small QF_LRA examples for checker development

Start with `certificates/00-boolean.sexp`, then cases 01–07. Cases 08–11
add Boolean structure and examples of already lowered equalities and `ite`.
There are 12 actual Primo outputs, totalling 4,092 bytes; the largest is
844 bytes. Every source problem is UNSAT. No Primo build is needed to read
or check the saved certificates.

These use the experimental **version-2 QF_LRA format**. They are examples for
developing arithmetic support, not inputs claimed to work with the current
Micromath checker. `inputs/*.smt2` are the original SMT-LIB problems;
`certificates/*.sexp` are the exact, unedited solver outputs to give the checker.

## Suggested order

| Certificate | What it exercises | Theory conflicts |
| --- | --- | ---: |
| [00-boolean](certificates/00-boolean.sexp) | Boolean contradiction, no arithmetic rules needed | 0 |
| [01-weak-gap](certificates/01-weak-gap.sexp) | Two weak inequalities give `0 <= -1` | 1 |
| [02-strict-boundary](certificates/02-strict-boundary.sexp) | Positive-weight strictness gives `0 < 0` | 1 |
| [03-three-premises](certificates/03-three-premises.sexp) | Cancellation across two variables and three premises | 1 |
| [04-scaling](certificates/04-scaling.sexp) | Non-unit integer multipliers: `1, 2, 1` | 1 |
| [05-rational-weight](certificates/05-rational-weight.sexp) | Rational multiplier `(/ 1 2)` | 1 |
| [06-rational-input](certificates/06-rational-input.sexp) | Rational input coefficient/bound and multiplier `(/ 10 3)` | 1 |
| [07-negated-premise](certificates/07-negated-premise.sexp) | Signed literal `(not (< x 0))` and a negative constant | 1 |
| [08-boolean-branches](certificates/08-boolean-branches.sexp) | Two theory clauses jointly refute a disjunction; Boolean auxiliaries | 2 |
| [09-equality](certificates/09-equality.sexp) | Source equality has already become two weak comparisons | 1 |
| [10-disequality](certificates/10-disequality.sexp) | Source disequality becomes Boolean structure; arithmetic transitivity is needed | 1 |
| [11-ite](certificates/11-ite.sexp) | Source Real `ite` and equalities are already lowered; two branch conflicts | 2 |

The last three do **not** require new equality or `ite` rules in the arithmetic
checker. Their exported theory premises are just signed inequalities. The
checker does need to connect the complemented conflicts with the CNF's Boolean
structure, including the declared `primo_cnf_*` constants.

## Arithmetic rule and polarity

The outer sections are `version`, `logic`, `signature`, `cnf`, and `conflicts`.
`(clause L1 L2 ...)` means the disjunction of its signed literals. For example,
`(clause p)` and `(clause (not p))` refute case 00 without theory lemmas.

Each record has the form

```lisp
(conflict-lra (L1 c1) (L2 c2) ...)
```

Here the `Li` are **conjunctively inconsistent facts**, not the learned clause.
The clause available to the SAT checker is `(not L1) or (not L2) or ...`.
The `ci` are exact nonnegative rational coefficients: an integer or `(/ N D)`
with nonnegative numerator and positive denominator. Zero is allowed.

Normalize each signed comparison to `ai*x <= bi` or `ai*x < bi`, multiply by
its coefficient, and add. Every variable coefficient must cancel. The resulting
bound must be negative, or zero with at least one strict premise of positive
weight. Negating `<` produces a weak opposite inequality; negating `<=`
produces a strict opposite inequality.

For case 05, the producer emits

```lisp
(conflict-lra ((<= (* 2 x) 0) (/ 1 2)) ((< 0 x) 1))
```

The first weighted premise is `x <= 0`; the second is `-x < 0`. Their sum is
`0 < 0`. In case 04, adding `2*x+y <= 1`, twice `-x <= -1`, and `-y <= 0`
instead gives the weak contradiction `0 <= -1`.

## Small acceptance/rejection controls

The three `controls/` files are deliberately authored mutations, **not**
unedited Primo outputs. Each starts from a saved certificate above and leaves
its version and logic unchanged.

| File | Expected | Change and reason |
| --- | --- | --- |
| [accept-zero-weight](controls/accept-zero-weight.sexp) | Accept | Extends case 01 with declared `y`, CNF fact `y <= 10` and that premise at weight zero. The original contradiction is preserved. |
| [reject-zeroed-weight](controls/reject-zeroed-weight.sexp) | Reject | Changes the first conflict weight in case 01 from 1 to 0. The variable coefficient no longer cancels. |
| [reject-lost-strictness](controls/reject-lost-strictness.sexp) | Reject | Changes only the strict conflict premise of case 02 to weak. Its weighted sum is merely `0 <= 0`. |

Both rejection cases retain the original CNF. The zeroed-weight case also
keeps the conflict's literals, so its Boolean UNSAT check still succeeds
despite the invalid weights. Check the arithmetic record itself.

## Checking and regeneration

The included standard-library Python checker is a development aid. From this
directory, check all file hashes, arithmetic witnesses and expected rejections:

```bash
python3 verify.py
```

It should report 13 accepted files and two expected rejections. To additionally
confirm propositional UNSAT of all accepted files with an external CaDiCaL:

```bash
python3 verify.py --sat-solver /path/to/cadical
```

Without `--sat-solver`, this checks arithmetic and syntax only. Even with it,
the SAT solver is trusted; this is not a propositional proof-kernel check.
Micromath integration remains to be developed. The exported CNF is the
starting problem; source-to-CNF proof is not included.

To regenerate one saved producer example with Primo:

```bash
primo --quiet --preprocessing-mode=off --export-cnf-reduction=core \
  --export-cnf regenerated.sexp inputs/05-rational-weight.smt2
```

Optional preprocessing is disabled so that these examples retain arithmetic
conflicts. Required native equality/`ite` lowering still runs. CORE selects
complete theory clauses before coefficients are reconstructed. Output bytes
can differ with another producer/backend version; use the saved files for
an exact shared example.

## Provenance and scope

The native outputs were generated by Primo
`e910cfdec073e0d3ba937fe9d95e6669e0eb7359`, Release, with CaDiCaL
`cadical-3.0.0-7b99c07`, in SLURM job **348286**. All 12 source files were
independently confirmed UNSAT by Z3. All 12 artifacts passed exact rational
witness checking and external CaDiCaL propositional confirmation. See
[manifest.json](manifest.json) for input/output hashes, counts and bounds.

These are small original development examples, not a real-benchmark sample or
a claim of checker compatibility. The bundle contains no external solver
source, binaries, credentials or connection configuration.
