# Smaller Farkas coefficients

**Status: experimental standalone adapter with bounded SLURM qualification.**
This is a separate post-processing problem on a completed version-2 QF_LRA
certificate. It does not change the solver, the selected theory clauses, their
premise order, the original CNF, or the certificate format. LIA cut derivations
are outside this first adapter: changing the sum behind an intermediate cut
requires preserving its conclusion, not merely finding any contradiction.

The {farkassource}`CLI and streaming transformer <scripts/minimize_lra_coefficients.py>`
own parsing, byte copying, external calls and publication. The pure
{farkassource}`arithmetic helper <scripts/farkas_coefficients.py>` owns candidate
generation, exact validation and the optimization encoding. The
{farkassource}`qualification driver <scripts/qualify_farkas_coefficients.py>`
compares the resulting certificates with the retained inputs and independent
checker. These are Python utilities; native C++ search and reconstruction are
unchanged.

```sh
python3 scripts/minimize_lra_coefficients.py original.sexp smaller.sexp --report metrics.json
python3 scripts/minimize_lra_coefficients.py original.sexp optimized.sexp \
  --optimizer /path/to/z3 --problems-dir coefficient-problems --report metrics.json
```

The default is cheap exact candidate generation only. `--optimizer` enables
the separate Optimize-compatible executable; its arguments precede the appended
`.smt2` path. `--problems-dir` without a backend only saves standalone problems.
The authored example below needs `--min-bits 0` to override the default policy
of skipping optimization when the cheap incumbent's bit height is below eight.
Here bit height is the maximum bit length of any nonzero reduced weight's
numerator or denominator.

## Reading scope and attribution

Hoenicke and Schindler, *A Simple Proof Format for SMT* (SMT 2022), §4.1 and
Figure 3, specify the integer-weighted `farkas` axiom and its cancellation and
strictness side conditions {cite:p}`hoenicke2022simple`. Section 6 describes
streaming proofs and sharing terms to avoid retaining/repeating proof history.
Figure 3 uses strictly positive integer weights. Our format permits rational
weights and zero positions: clear denominators by a positive common multiple,
omit zero-weight premises when applying that axiom, then weaken the resulting
theory clause with the complements of the omitted premises. This explains the
deviation; we check the equivalent rational cancellation condition directly.
Christ, Hoenicke and Nutz, *Proof Tree Preserving Interpolation*, §5
{cite:p}`christ2017proof`, explains nonnegative Farkas combinations and why a
purely integer inconsistency may need additional reasoning beyond one linear
combination. These sections were read from the authors' PDFs, not solver code.

These sources justify the proof contract and the separation of propositional
structure from arithmetic witnesses. They do **not** describe the min-max
coefficient optimization below. The objectives, cheap candidate sequence and
acceptance policy are our design, not an implementation of an optimized
SMTInterpol algorithm. We do not copy its implementation or change its format.

## Proof obligation

Normalize each original signed fact to $a_i x\le b_i$ or $a_i x<b_i$.
For a candidate weight vector $w$, require

$$
w\ge0,\qquad \sum_i w_i a_i=0,\qquad
B:=\sum_i w_i b_i<0
\quad\text{or}\quad
(B=0\ \land\ \sum_{i:\mathrm{strict}(i)}w_i>0).
$$

The strictness test ignores zero weights. The all-zero vector is invalid.
The helper's {farkassource}`valid <scripts/farkas_coefficients.py#L38>` operation
implements this predicate. Every original and replacement vector must pass
exact rational replay before
publication; optimizer status/model text alone never establishes validity.
Only weight spans may change. Zero-weight premise positions remain present,
so the complemented theory clause and any propositional clause references are
unchanged. This is coefficient simplification, not MUS or literal deletion.

## Cheap candidates before optimization

The {farkassource}`cheap <scripts/farkas_coefficients.py#L72>` operation tries
the following bounded candidates.

1. Clear common denominators and divide the resulting integers by their GCD.
   This removes a common scale. It is not automatically cheaper: many unrelated
   denominators can make their LCM enormous, so bound construction and compare
   the candidate before retaining it.
2. Try unit weights on the existing support and on all premises. Also try exact
   rational rescaling by the smallest/largest positive weight and small
   denominator approximations. Approximation is only a candidate generator;
   exact replay must still succeed.
3. Merge weights of identical normalized premises, keeping their original
   positions and setting redundant positions to zero. Re-normalize the result.
4. Look for a contradictory constant fact or two opposite proportional rows.
   Group rows by their first-coefficient-normalized sparse vector and retain
   the tightest bound of each sign, preferring strictness at equal bounds.
   Verify each proposed one/two-premise combination exactly.

These are bounded heuristics. They neither shrink the input theory clause nor
promise a minimum-support witness. Sparse maps own rational coefficients;
there are no term-manager, SAT, tableau or parser-node borrows across records.
An initial cost measures the largest numerator/denominator bit length, total
serialized weight bytes and positive-weight count. Accept only a strict
lexicographic improvement that increases neither the largest bit length nor
serialized weight bytes. These are cost proxies, not a proof of faster checking.

## A separate integer optimization problem

Use integer unknowns $z_i\ge0$. For each cancellation equation, clear rational
denominators and remove a common integer factor; the equation remains exact.
Do the same for the vector of bounds, obtaining an integer linear form
$\widehat B$. Contradiction becomes the non-strict integer formula

$$
\widehat B\le-1\quad\lor\quad
(\widehat B=0\land\sum_{i:\mathrm{strict}(i)}z_i\ge1).
$$

Introduce $t$ with $z_i\le t$, then minimize $t$ and, lexicographically,
$\sum_i z_i$. This is QF_LIA with optimization commands, not nonlinear
arithmetic. Homogeneity explains why integer candidates are sufficient to
represent some witness whenever a rational witness exists: multiply by the
LCM of denominators. It does not make every primitive integer vector smaller
than the original rational one.

Bound $t$ by the incumbent's largest bit length and the configured encoding
budget. This deliberately searches only candidates that can avoid worsening
that proxy; bounded infeasibility is not evidence that the original lemma is
satisfiable. General integer optimization can be expensive. Per-call time,
total time, record/premise size and query-count limits are required.

The helper's {farkassource}`optimization_problem <scripts/farkas_coefficients.py#L146>`
builds this encoding; {farkassource}`Optimizer.improve <scripts/minimize_lra_coefficients.py#L174>`
owns external calls and fallback. The external backend accepts an SMT-LIB file
and returns `sat` plus integer
`get-value` assignments. The first supported backend is Z3's Optimize protocol;
its documented lexicographic objective order is used. No optimization solver
code is implemented or copied. Timeout, unknown, nonzero exit, malformed model,
invalid arithmetic or a worse proxy keeps the checked cheap incumbent. Report
successful candidate adoption separately from a claim of global optimality.
Optional saved `.smt2` problems let the author inspect/solve each optimization
independently of the original SMT query.

The defaults permit 32 problems, 256 premises per optimization, a 4,096-bit
integer-encoding budget, one second per external call and 30 seconds summed
external-call time. Child processes have a 3 GiB address-space and 16 MiB file
limit. The CLI report distinguishes its internal processing timer from summed
external-call wall time. The internal timer excludes interpreter startup,
argument parsing and final hashing/report/publication. Qualification separately
measures the complete adapter process from outside. These call
budgets are not a hard total limit on the complete adapter. Z3's Optimize
protocol and lexicographic ordering are documented in its
[official guide](https://microsoft.github.io/z3guide/docs/optimization/combiningobjectives/).

## A nontrivial optimization example

The {download}`authored certificate <../examples/farkas-coefficient-choice.sexp>`
has rows $(31,0)$, $(0,31)$, $(-7,-20)$ and $(-34,-13)$, with bounds
$(0,0,0,-1)$. Weights $(11,7,5,9)$ cancel both columns and give bound $-9$.
There is no opposite pair, and unit weights do not cancel. The separate integer
problem admits $(2,3,4,1)$, which cancels both columns and gives bound $-1$.
Its maximum weight is four instead of eleven. This example is newly authored;
it is not an original solver output or evidence of a general performance gain.

## Ownership, streaming and publication checklist

The CLI's {farkassource}`transform <scripts/minimize_lra_coefficients.py#L238>`
operation owns the streaming pass; `main` owns temporary files and final publication.

- Read declarations once; keep the sort map and one weighted conflict at a time.
  Skip/copy the initial CNF in bounded chunks, without a full-file syntax tree.
- The byte lexer buffers 64 KiB blocks and at most a 1 MiB token; individual
  expressions and complete weighted records have a 64 MiB source-byte limit.
  These are parser/resource limits, not claims that malformed or unsupported
  input has become satisfiable. Initial clauses are balanced/copied, not
  propositionally validated by this coefficient adapter.
- Retain byte offsets for weight replacements; copy every other input byte.
  Facts, declarations, clauses and their order must remain byte-identical.
- Use exact `Fraction` arithmetic for normalization, cancellation and strictness.
  Reuse the existing arithmetic reader, not a second solver's implementation.
- Stage output in the destination directory and atomically replace it only
  after parsing and arithmetic checking finish. Reject in-place input/output.
  Failure preserves any previous destination. External candidate failure is
  a fallback, whereas malformed original witnesses are fatal.
- Record input/final maximum bit size, serialized bytes, nonzeros,
  changed lemma count, query counts/status/time and fallback reasons.
- Keep the CLI optional. No native option/default, solver search, CORE/MUS
  selection, or QF_LIA proof step is changed.

## Qualification checklist

Pure tests must cover common-scale elimination, denominator-LCM blow-up,
support alternatives, strictness with zero weights, signed facts, malformed
optimizer output, nonzero-exit/model rejection, fallback and atomic publication.
Check byte identity outside weights, including quoted/multiline identifiers.
Use fresh generated exact witness cases and saved real CORE certificates in
SLURM. Replay original and simplified certificates in Micromath with fixed
limits, recording coefficient statistics and kernel steps/times separately
from adapter/optimizer time. Coefficient reduction alone is not a checker-speed
claim. No source-to-CNF proof or complete LIA coefficient optimization follows.

## Bounded qualification, 10 November 2026

The {download}`qualification manifest <../coverage/farkas-coefficients-20261110.json>`
and {download}`compact replay archive <../../benchmarks/checker-regressions/farkas-coefficients-20261110/results.tar.gz>`
retain source/binary hashes, commands, seed, resource limits, individual results,
saved optimization problems and diagnostics. The archive contains certificates
and logs, not expanded proof streams. Private absolute paths in metadata are
replaced with environment names. The canonical entry is in
{download}`the run log <../run-log.json>`.

The adapter and arithmetic helper are pinned to implementation `180dcaa8`.
SLURM job `382321_0` checked 1,000 newly generated cases with seed `20261110`:
750 valid witnesses passed exact, structure-preservation and CaDiCaL checks;
all 250 SAT negative controls were rejected without publishing a certificate.
Pinned Z3 agreed on every original SMT-LIB formula. There were no unexpected
rejections, crashes, reference disagreements or timeouts. Fifty-one valid cases
also invoked the optimizer; none improved the already simplified incumbent.
This is a proof-postprocessor gate, not a new native solver fuzz campaign.

Job `382323_1` checked twelve saved native starter certificates, one large real
CORE certificate (clock-7), and the authored example, in original/cheap/optimized
profiles. All 42 profiles passed independent Micromath proof generation and
kernel verification at revision `62353d2d`, with both process exits zero and
`challenge proven`, without `Inconsistency proven!`. This job supersedes the
initial six-profile replay `382321_1`, whose archive-prefix bug omitted the
starters. The corrected harness requires exactly twelve starters. The final
shared runner's interruption cleanup was verified separately by all 14 focused
unit tests in job `382324`; it changes cancellation, not the measured normal path.

| Input/profile | Maximum weight bit height | Weight text bytes | LRA kernel steps | Proof + kernel seconds | Adapter wall seconds |
| --- | ---: | ---: | ---: | ---: | ---: |
| Clock-7 original | 68 | 213,349 | 125,850,923 | 52.697 | — |
| Clock-7 cheap | 59 | 73,254 | 125,850,923 | 52.294 | 5.478 |
| Clock-7 optimized | 59 | 73,254 | 125,850,923 | 52.545 | 8.334 |
| Authored example original | 4 | 5 | 42,712 | 0.419 | — |
| Authored example cheap | 4 | 5 | 42,712 | 0.418 | 0.220 |
| Authored example optimized | 3 | 4 | 30,062 | 0.418 | 0.270 |

Clock-7 changes 415 of 423 witnesses and reduces the complete certificate from
626,049 to 485,954 bytes. Its 32 bounded optimizer queries find no further
improvement. The unchanged arithmetic step count shows that common-scale
simplification saves text without helping this checker's arithmetic proof.
The authored example changes weight ratios and reduces those steps by 29.6%,
but subsecond wall times do not establish a timing gain. The twelve starters
also have unchanged arithmetic kernel steps across profiles. These are single
passes on a small set, not a full-corpus speed claim. The table uses externally
measured adapter wall time, including startup and publication. Optimizer cost is
already included in that time; add adapter and proof time when judging total work.
