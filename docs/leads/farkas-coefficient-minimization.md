# Smaller arithmetic certificate coefficients

**Status: experimental standalone LRA/LIA adapter with bounded SLURM qualification.**
This is a separate post-processing problem on a completed version-2 QF_LRA
or version-3 QF_LIA certificate. It preserves the selected theory clauses,
premise/reference order, derived integer inequalities, CNF and certificate
format. The native solver is unchanged. The historical CLI name is retained
for both logics; only arithmetic weight/divisor spans can change.

The {coefficientextensionsource}`CLI and streaming transformer <scripts/minimize_lra_coefficients.py>`
own parsing, byte copying, external calls and publication. The pure
{coefficientextensionsource}`arithmetic helper <scripts/farkas_coefficients.py>` owns candidate
generation, exact validation and the optimization encoding. The
{coefficientextensionsource}`extension qualification driver <scripts/qualify_arithmetic_coefficients.py>`
compares the resulting certificates with the retained inputs and independent
checker. These are Python utilities; native C++ search and reconstruction are
unchanged.

```sh
python3 scripts/minimize_lra_coefficients.py original.sexp smaller.sexp --report metrics.json
python3 scripts/minimize_lra_coefficients.py original.sexp optimized.sexp \
  --optimizer /path/to/z3 --problems-dir coefficient-problems --report metrics.json
python3 scripts/minimize_lra_coefficients.py integer-v3.sexp smaller-v3.sexp \
  --objective support --optimizer /path/to/z3 --report integer-metrics.json
```

The default is cheap exact candidate generation only. `--optimizer` enables
the separate Optimize-compatible executable; its arguments precede the appended
`.smt2` path. `--problems-dir` without a backend only saves standalone problems.
The authored example below needs `--min-bits 0` to override the default policy
of skipping optimization when the cheap incumbent's bit height is below eight.
For LRA, bit height is the maximum bit length of any nonzero reduced weight's
numerator or denominator; for LIA it also includes the integer divisor.

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

The [expanded primary-source survey](../papers/arithmetic-certificate-optimization.md)
adds interpolation-directed constraint solving, sparsity, decomposition and
integer-certificate lifetime management. In particular, *Beautiful Interpolants*
§4/Appendix B.3 is relevant prior art for constraining Farkas witnesses and
separator simplicity. Our implemented objective remains a bounded min-max/L1
baseline. The extension below adds sparse objectives and fixed-conclusion LIA
optimization; interpolation-directed synthesis remains unimplemented.

## Support and fixed integer cuts

The adapter extension uses *Beautiful Interpolants*, Appendix B.3,
only for the sparse-witness objective: minimize the number of positive
multipliers before numerical magnitude. It does not implement that paper's
separator synthesis, sample merging or interpolation algorithm. The exact
integer-cut constraints are derived in the
[survey](../papers/arithmetic-certificate-optimization.md#lia-preserving-a-cut-versus-choosing-an-interpolant),
from our version-3 cut contract rather than another solver's implementation.

- Keep `size` as the default objective. An explicit `support` objective counts
  positive weights, then minimizes the largest numeral and their sum. Candidate
  adoption must still increase neither maximum bit height nor numeral bytes.
- `transform_integer` owns its row table. `lia_coefficients.py` provides exact
  cut replay, joint weight/divisor
  GCD reduction and the fixed-conclusion integer optimization problem. Every
  replacement must reproduce both the original coefficient map and floored
  bound; divisors and weights remain integers with positive divisor and at
  least one positive weight.
- The streaming CLI keeps premise/reference positions, names, ordering and CNF
  bytes. Only weight/divisor spans may change. External model failure keeps
  the checked incumbent; invalid original arithmetic aborts atomic publication.
- For LIA, scan each proof's references before replay, retain last-use indices,
  and retire row-table entries after their last dependency. This adopts the
  lifetime idea in VIPR §5, not its full certificate conversion algorithm.
  State comprises the label index, row table and current-instruction rows, with a
  source-byte limit per conflict; no file-sized syntax tree is retained.
- Test nontrivial support alternatives, negative floors, exact divisibility,
  joint GCD, duplicate references, zero-weight references, downstream cut
  preservation, malformed models and publication rollback. Qualify freshly
  generated LRA/LIA cases and unchanged real certificates in SLURM against
  exact replay, a propositional backend, a source reference and Micromath.
  Report support, numeral sizes, released/live rows and query outcomes; no
  checker-speed claim follows merely from smaller coefficients.

### Representation and exact integer encoding

{coefficientextensionsource}`transform_integer <scripts/minimize_lra_coefficients.py>`
owns a table of `(dict[str, int], int)` rows. The arithmetic helpers receive
references to operand rows, never mutate them, and return fresh result maps.
{coefficientextensionsource}`replay_cut <scripts/lia_coefficients.py>` sums integer rows, checks positive divisor/nonnegative weights
and exact coefficient division, then uses Python floor division on the bound.
{coefficientextensionsource}`cheap_cut <scripts/lia_coefficients.py>` tries joint GCD reduction, unit weights, divisor one and duplicate-row
weight merging. Every candidate must reproduce the exact original row and
floored bound. All-zero cut weights are rejected, matching Micromath's rule.

{coefficientextensionsource}`cut_optimization_problem <scripts/lia_coefficients.py>` fixes the original conclusion $a x\le b$ and
introduces integer weights $w_i\ge0$ and divisor $d\ge1$ satisfying

$$
\sum_i w_i a_i=d a,\qquad
 d b\le\sum_i w_i b_i<d(b+1),\qquad\sum_i w_i\ge1.
$$

The strict upper bound is over integers, hence equivalent to the survey's
upper bound $d(b+1)-1$. Minimize a maximum $t$ bounding both weights and divisor,
then $d+\sum_iw_i$. With `--objective support`, first minimize
$\sum_i\operatorname{ite}(w_i>0,1,0)$; the divisor is not a support position.
Constants and the incumbent maximum numeral height must fit `--max-bits`.
{coefficientextensionsource}`Optimizer.improve_cut <scripts/minimize_lra_coefficients.py>` exact-checks every model and keeps the checked incumbent
on any failure. The support objective uses the same numeral guards as `size`:
maximum bit height and combined serialized weight/divisor bytes cannot grow.
The guards also apply to cheap passes; this is bounded heuristic simplification,
not minimum support, minimum numerals or a cut-reconstruction algorithm.

For example, $-2x\le-1$ with weight 100 and divisor 200 derives
$-x\le-1$. Reducing **both** to weight 1/divisor 2 preserves the negative floor.
The {download}`authored four-row example <../examples/lia-coefficient-choice.sexp>`
uses weights $(11,7,5,9)$ and divisor 9 to derive $0\le-1$; weights
$(2,3,4,1)$ and divisor 1 derive the same conclusion. Unit weights and joint GCD
alone cannot find that change. These are authored examples, not performance
claims about native outputs.

`transform_integer` parses premises,
then scans proof references to record last-use instruction indices. A second
scan replays all cuts, emits numeral replacements immediately, and retires
row-table entries after their last use (including syntactic zero-weight references). Duplicate
references are summed and released once; forward references and duplicate
labels are rejected. The final named row must be a contradiction. Unused cuts
are still validated and retained in the output. The adapter does not prune or
reorder the proof. Lifetime bookkeeping follows VIPR §5, without adding any
last-use field to the certificate.

The lexer has the existing 1 MiB token and 64 MiB expression limits; LIA also
limits each source conflict to 64 MiB. Proof-token reads enforce that bound
during reference indexing and replay, before accumulating additional references. It holds label/last-use metadata, the row table and transient per-instruction
operand/result rows, without retaining the whole proof text or rows past their
last use. A wide dependency graph can still require many rows concurrently.
The operand list is dropped before table entries are retired; transient result
maps have bounded per-instruction lifetime. Two scans
cost linear parsing work in proof bytes, in addition to exact arithmetic and
bounded optional optimization. Big-integer arithmetic costs depend on numeral
sizes; these bounds are not a total processing-time guarantee. Reports add
`cuts`, `changed_cuts`, `peak_live_rows`, `released_rows`, and separate divisor
bytes. `peak_live_rows` samples row-table occupancy, including all initial
premises before unused ones are retired; it is not a count of all physically
live Python row objects. `released_rows` counts retired rows, including unused
cuts whose result is never inserted; it excludes the terminal contradiction row. Maximum bit height includes divisors for LIA, and nonzero counts include
weights only. LRA reports retain their previous interpretation.

## LRA proof obligation

Normalize each original signed fact to $a_i x\le b_i$ or $a_i x<b_i$.
For a candidate weight vector $w$, require

$$
w\ge0,\qquad \sum_i w_i a_i=0,\qquad
B:=\sum_i w_i b_i<0
\quad\text{or}\quad
(B=0\ \land\ \sum_{i:\mathrm{strict}(i)}w_i>0).
$$

The strictness test ignores zero weights. The all-zero vector is invalid.
The helper's {coefficientextensionsource}`valid <scripts/farkas_coefficients.py>` operation
implements this predicate. Every original and replacement vector must pass
exact rational replay before
publication; optimizer status/model text alone never establishes validity.
Only weight spans may change for LRA. Zero-weight premise positions remain present,
so the complemented theory clause and any propositional clause references are
unchanged. This is coefficient simplification, not MUS or literal deletion.

## Cheap candidates before optimization

The {coefficientextensionsource}`cheap <scripts/farkas_coefficients.py>` operation tries
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
An adoption cost measures the largest numerator/denominator bit length, total
serialized weight bytes and positive-weight count. The default `size` adoption
order is `(bits, bytes, support)`; `support` adoption orders these proxies as
`(support, bits, bytes)`. Both require a strict improvement and neither size
guard may grow. For LIA, size includes the divisor, while support counts only
weights. These adoption orders differ from the external solver objectives
`(maximum integer, integer sum)` or `(support, maximum integer, integer sum)`. These are cost proxies, not a proof of faster checking.

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
$\sum_i z_i$. `--objective support` prepends the positive-weight count to
that solver objective order. It does not delete premise positions. This is QF_LIA with optimization commands, not nonlinear
arithmetic. Homogeneity explains why integer candidates are sufficient to
represent some witness whenever a rational witness exists: multiply by the
LCM of denominators. It does not make every primitive integer vector smaller
than the original rational one.

Bound $t$ by the incumbent's largest bit length and the configured encoding
budget. This deliberately searches only candidates that can avoid worsening
that proxy; bounded infeasibility is not evidence that the original lemma is
satisfiable. General integer optimization can be expensive. Per-call time,
total time, record/premise size and query-count limits are required.

The helper's {coefficientextensionsource}`optimization_problem <scripts/farkas_coefficients.py>`
builds this encoding; {coefficientextensionsource}`Optimizer.improve <scripts/minimize_lra_coefficients.py>`
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

The CLI's {coefficientextensionsource}`transform <scripts/minimize_lra_coefficients.py>`
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
- Keep the CLI optional. No native option/default, solver search or CORE/MUS
  selection changes. LIA cut conclusions remain exact.

## Qualification checklist

Pure tests must cover common-scale elimination, denominator-LCM blow-up,
support alternatives, strictness with zero weights, signed facts, malformed
optimizer output, nonzero-exit/model rejection, fallback and atomic publication.
Check byte identity outside weights, including quoted/multiline identifiers.
Use fresh generated exact witness cases and saved real CORE certificates in
SLURM. Replay original and simplified certificates in Micromath with fixed
limits, recording coefficient statistics and kernel steps/times separately
from adapter/optimizer time. Coefficient reduction alone is not a checker-speed
claim. No source-to-CNF proof or complete interpolation procedure follows.

## Bounded qualification, 10 November 2026

The {download}`qualification manifest <../coverage/farkas-coefficients-20261110.json>`
and {download}`compact replay archive <../../benchmarks/checker-regressions/farkas-coefficients-20261110/results.tar.gz>`
retain source/binary hashes, commands, seed, resource limits, individual results,
saved optimization problems and diagnostics. The archive contains certificates
and logs, not expanded proof streams. Private absolute paths in metadata are
replaced with environment names. The canonical entry is in
{download}`the run log <../run-log.json>`.

The following historical LRA measurements qualify
{farkassource}`implementation 180dcaa8 <scripts/minimize_lra_coefficients.py>`,
not the later LIA/support extension.
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


A [follow-up on fifteen real LRA timeout cases](../evidence/checker-replay-20261110.md#completed-coefficient-comparison)
found no additional verification through simplification: each transformed
profile verifies the same four cases, ten checker pipelines time out, and the
largest adapter input reaches its separate processing cap. The 448 optimizer
queries in completed outputs adopt no candidate. This strengthens the reason
to keep optimization optional; the authored step reduction is not evidence of
routine benefit on real certificates.


## LIA and support qualification, 11 November 2026

The [final extension qualification](../evidence/arithmetic-coefficients-20261111.md)
pins implementation `9b4b490f`. Fresh SLURM jobs `382370_0..3` passed 1,000
source/reference cases per logic (750 valid witnesses and 250 SAT controls
for each), all 80 independent Micromath profiles and all 29 focused tests.
The [ledger](../coverage/arithmetic-coefficients-20261111.json) and compact
[replay archive](../../benchmarks/checker-regressions/arithmetic-coefficients-20261111/results.tar.gz)
retain hashes, commands, inputs, optimization problems/models and diagnostics.

The authored LIA example reduces arithmetic kernel steps from 26,808 to 15,198
by changing weights and divisor while preserving the exact final inequality.
A separate authored probe makes `support` select three premises while `size`
uses four with smaller numerals; the latter needs fewer arithmetic steps on
that example. These observations justify keeping both objectives explicit and
`size` as default. All representative originals already verify; this is not
full-corpus coverage, a general speed result or interpolation evaluation.


The [medium-cost success set](../evidence/checker-medium-20261111.md) adds eight
real CORE-selected cases that currently verify in 28–74 seconds, plus one
98-second reserve. It retains inputs and witness metrics from nine benchmark
families for future paired experiments. These discovery runs check the original
certificates; they do not measure a coefficient-optimization speedup.

The [three-case medium comparison](../evidence/checker-medium-comparison-20261111.md)
now checks those profiles on Rings, LassoRanker and Clock7 main invariant. All
12 pipelines verify, but arithmetic steps are unchanged and size/support return
the same bytes as the cheap pass. The 12–24 second size/support adapter overhead gives no
checking gain here. Profiling precedes further optimization work.
