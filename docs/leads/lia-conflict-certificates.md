# Experimental integer-only LIA conflict derivations

**Status: experimental bounded implementation; bounded SLURM checks completed with native-search limits reported below.** The existing UF/version-1 and
[LRA/version-2 formats](../reference/qf-lra-certificates.md) remain unchanged.
Version 3 adds `QF_LIA`, `Int` constants and `conflict-lia` records.
The exported CNF remains the starting problem; this format supplies no proof
of source-to-CNF conversion.

## Why local names help

The current LRA conflict is a sequence of `(literal coefficient)` pairs. It
asks for one weighted sum of the original premises, with cancellation and a
contradictory constant/strictness result. Names are unnecessary for that single
sum. Clearing the multipliers' denominators can make those weights integers
without changing the proof's meaning.

An integer derivation may instead combine premises, round a resulting bound,
then combine that derived inequality with another premise. Local names let a
later step reference or reuse an earlier result without repeating its formula.

| Property | Current LRA record | Experimental LIA record |
| --- | --- | --- |
| Conflict meaning | Listed signed literals are conjunctively inconsistent | Same |
| Premise representation | Literal/weight pairs | Named signed literals under `premises` |
| Justification | One weighted sum | A sequence of weighted sums with optional integer bound tightening |
| Names | None needed | Local labels for premises and derived inequalities |
| CNF/SAT boundary | Add complements of the listed literals as a theory clause | Same; proof-step labels never enter SAT |

These names belong to one conflict record. They are not SMT declarations,
Primo term IDs, SAT variables, or a global catalogue of facts. Premise and step
labels must be unique within their record, and a step may reference only
premises or earlier steps in the same record. Another record may reuse the
same label.

## One initial arithmetic instruction

```lisp
(cut NEW-ID DIVISOR (REFERENCE WEIGHT) ...)
```

The checker first normalizes each referenced premise or step to
$a_i\cdot x\le b_i$, where every variable is declared `Int` and all polynomial
coefficients and constants are integers.

Here $x$ denotes the vector of integer variables, $a_i$ its coefficient vector
in the referenced inequality, and $b_i$ that inequality's constant bound.
The weighted sum is

$$A=\sum_i w_i a_i,\qquad B=\sum_i w_i b_i.$$

Every weight $w_i$ must be a nonnegative integer. The divisor $d$ must be a
positive integer dividing **every variable coefficient** in $A$. It need not
divide $B$. The result named by `NEW-ID` is computed by the checker:

$$\frac{A}{d}\cdot x\le\left\lfloor\frac{B}{d}\right\rfloor.$$

No rational numeral is stored: coefficient division is exact, and the bound
uses integer floor division. The rule is sound because the resulting
left-hand side is integer-valued. Divisor 1 gives ordinary summation.
All arithmetic needs arbitrary-precision integers. In particular,
$\lfloor-1/2\rfloor=-1$; truncation toward zero is not valid here.

Initial premises are signed affine comparisons. A strict integer comparison
$P<b$ becomes $P\le b-1$. Negation is applied before this conversion: for
example, $\neg(P\le b)$ becomes $-P\le-b-1$. Preserve the original signed
literal in `premises`; this normalization is internal to the checker.
Source equalities must be lowered to inequalities before using this initial
fragment. Real-valued tableau auxiliary variables must never be treated as
integer-valued merely because the source logic is QF_LIA.

```lisp
(contradiction REFERENCE)
```

This must be the final proof instruction. The referenced inequality must have
all variable coefficients zero and a negative bound. Successfully checking it
justifies the clause containing the complements of **all original premise
literals**, not the complements of derived cuts. Unused listed premises are
allowed; retaining them preserves the selected theory clause. No CNF clause
outside the record supplies an implicit assumption for its derivation.

## A small example combining inequalities and rounding

The {download}`SMT-LIB input <../examples/qf-lia-certificate-proposal.smt2>` is
the conjunction of the following three facts, with integer $x,y$:

$$
a:\ 3x+2y\le4,\qquad b:\ y\ge1,\qquad c:\ 2x\ge1.
$$

Its real relaxation is satisfiable: $x=1/2,y=1$ satisfies all three. Consequently,
no weighted-sum contradiction of the LRA kind can refute these original facts.
Each fact is also necessary for this conjunction's integer infeasibility:
removing $a$, $b$, or $c$ admits the integer assignment $(1,1)$, $(1,0)$, or
$(0,1)$ respectively.

The separate {download}`proposed certificate <../examples/qf-lia-certificate-proposal.sexp>`
keeps the same signature/CNF/conflicts container and uses this conflict:

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

The checker derives the following results; they are explanations, not extra
trusted annotations in the file:

| Step | Integer computation | Derived inequality |
| --- | --- | --- |
| `upper` | Normalize $b$ to $-y\le-1$; add $a+2b$ to get $3x\le2$; divide by 3 and floor the bound | $x\le0$ |
| `lower` | Normalize $c$ to $-2x\le-1$; divide by 2 and floor the bound | $-x\le-1$ |
| `impossible` | Add the previous two inequalities; divisor 1 | $0\le-1$ |

Thus the theory lemma is $\neg a\lor\neg b\lor\neg c$, where these symbols
abbreviate their original signed literals. They are not new Boolean variables.
In this example the CNF asserts each fact as a unit clause, so adding that
theory lemma closes the propositional refutation.

## Scope and integration

The [version-3 implementation](../reference/qf-lia-certificates.md) reconstructs
bounded cut derivations after theory-clause selection. Some integer conflicts
remain beyond this baseline. The main solver's
{ref}`integer explanation <lia-explanations>` is represented by
{lraexportsource}`LiaExplanation <src/theory/arithmetic/lia_explanation.hpp>`, which stores
antecedent literals from cuts, GCD checks and exhausted branches, without an
external derivation. Reconstruction uses the selected original premises rather
than retaining main-search history. Direct branch-and-bound replay needs a
scoped integer split rule and verification of both closed branches; it is not
defined by this initial syntax.

Keep the existing post-CORE/MUS objective: select complete theory clauses first,
then generate the derivation only for retained clauses. The implementation
requires integer tightening/elimination in addition to rescaling LRA witnesses.
The old starter archive contains actual version-2 outputs and remains unchanged;
the files linked here are explicitly hand-written version-3 proposals.

## Sources and implementation status

Nonnegative linear combination is the same mathematical operation used by the
[LRA proposal](lra-conflict-certificates.md#independent-arithmetic-check).
Integer coefficient normalization and bound tightening are described in
Pugh's expanded *Omega Test*, §2.1 {cite:p}`pugh1992omega`. Our `cut` syntax
combines those operations into one explicit, checked inference; this is not an
implementation of the Omega algorithm. Names, record layout and the example
are our format design. A native serializer, bounded reconstructor and separate
Python checker are implemented. The [qualification record](../reference/qf-lia-certificates.md#independent-checks-and-qualification) reports the bounded checks and native-search limits.

## Implementation checklist: bounded reconstruction

The implemented owners are the
{liaexportsource}`arithmetic proof types and entry points <src/theory/arithmetic/lia_certificate.hpp>`,
{liaexportsource}`SMT collector <src/smt/cnf_export.hpp>`,
{liaexportsource}`component capture <src/smt/component_solver.cpp#L71>` and
{liaexportsource}`independent serialized-proof checker <scripts/check_lia_certificate.py#L97>`.

Implementation work uses Pugh §2.1 (integer coefficient GCD and bound
tightening) and §2.3.1 (pairwise Fourier–Motzkin real-shadow elimination), with the
exact-projection preference of §2.3.2.
This is a bounded refutation baseline, not the complete Omega algorithm:
dark shadows, splinters and scoped branch replay are excluded. Failure to
derive a contradiction is inconclusive, never evidence of satisfiability.

- `theory::arithmetic::LiaCertificatePremise` owns sparse integer coefficients
  and an integer bound, with opaque numeric variable identities. The SMT
  collector supplies signed, strict-normalized original `Int` premises.
- `LiaCertificateCut` owns a positive divisor and nonnegative integer weights
  on backward references. Premise positions and derived step positions share
  a private numeric sequence; serialization assigns local `pN`/`sN` names.
  No solver term, SAT object or parser AST is retained by arithmetic proof state.
- Normalize each active row by its nonzero coefficient GCD, flooring the bound;
  record the operation as a cut when it changes the row. A constant negative
  bound is a contradiction. Combine opposite-sign bounds to eliminate one
  variable, record their positive integer multipliers and GCD tightening, and
  carry rows not containing that variable to the next projection.
- First try the private LRA witness reconstruction on tightened rows, clearing
  weight denominators before recording an integer sum. Otherwise prefer an
  exact projection (unit coefficients on one whole side), then a small
  positive/negative pair count. Remove
  duplicate left-hand sides while retaining the strongest bound. This is a
  basic sparse-map implementation; it does not implement Pugh's optimized
  constraint-key machinery. Intermediate rows/cuts have explicit count and
  integer-digit limits; cancellation and the export phase budget are polled.
  Pair generation can be quadratic per projection and exponential overall;
  exact integer costs also depend on operand bit lengths.
- A separate arithmetic replay verifies every divisor, type/integer condition,
  nonnegative weight, backward reference and computed conclusion before export.
  Only ancestors of the final contradiction need serialization; preserve all
  selected original premises in the record even if some are unused.
- Capture actual clausifier clauses and delivered component reasons/conflicts
  at the SAT boundary. Apply CORE/MUS before reconstruction; spool raw numeric
  lemmas and stage proof text on disk. Main solving has no proof provenance.
- Validate negative floor, GCD tightening, combination before tightening,
  repeated references, zero weights, rationally inconsistent cases, true integer
  conflicts, incomplete projection, resource limits and interruption. Mutate
  serialized proofs and check them independently using Python integers.
  Record generated steps, pairs, projections, pruned steps and completion.


## External kernel replay

Micromath now implements this integer cut rule. Its
[separate LIA route](../reference/qf-lia-certificates.md#micromath-integer-proofs-and-envelope-compatibility)
proved the original integer example using both the two-cut native derivation
and the three-cut hand-written derivation. At the qualified revision, the
container header must be version 2: compatibility copies change that byte only,
while native output stays version 3. This does not change the arithmetic rule,
erase integer sorts, or validate Primo's preprocessing. The recorded checks are
small examples and controls, not complete QF_LIA coverage.


## Opposing-bound reconstruction follow-up

The producer limit in `dillig/25-22.smt2` (array `362736`, the same case as
`362720`) motivated two narrow candidates, both rejected after targeted
measurement. The baseline native implementation remains `a5123159`. Pugh's expanded Omega Test,
§2.3, checks directly contradictory opposing inequalities before projecting;
§2.4 describes optimized opposing constraint keys. Primo's current baseline
already keeps an ordered `Active` map keyed by sparse coefficient maps, but
only merges bounds with identical left-hand sides. That accepted baseline
does not perform the opposing-bound check during insertion.

The following checklist was written before implementing the first candidate:

- Keep each active row primitive after coefficient-GCD tightening, and retain
  the strongest bound for each coefficient key. For `a*x <= b`, look up the
  key `-a` in the same map. If the stored bound is `-a*x <= c` and `b+c < 0`,
  emit a divisor-one cut with weights one on the two row references.
- Store only numeric references in proof steps. Snapshot both references and
  form the derived row before appending can invalidate references into the
  growing row vector. The existing proof pruner and independent replay must
  validate the resulting contradiction.
- Check while inserting initial tightened rows, carried rows and new projected
  rows. Stop projection immediately once a contradiction is available.
  Preserve all resource/cancellation gates and inconclusive outcomes.
- Count opposing lookups and successful contradictions. Add a regression in
  which projection exposes opposing tightened bounds under a pair budget too
  small for another elimination, plus satisfiable opposing bounds.
- This is an ordinary ordered-map baseline of the §2.3 check. It does not
  implement §2.4's expected-constant-time constraint-key machinery. Constructing
  an opposite key is linear in its nonzeros; ordered-map lookup adds map/key
  comparison cost. Worst-case projection remains exponential.

Candidate `b6fa115c` implemented the checklist in an isolated worktree and
passed the pair-budget unit regression in SLURM probe `362740`. The real target
still returned `unknown` with 5,224 generated cuts and 5,186 pairs; its 5,513
opposing lookups found no contradiction. The candidate was not accepted.


### Recheck the linear relaxation after new rounding

The first opposing-bound candidate passed its pair-budget regression but did
not reconstruct the real `dillig` case. Pugh §2.1 explicitly observes that
integer floor tightening can remove all rational solutions. The second candidate
therefore composed the existing exact LRA witness reconstructor with each
completed projection that introduced a genuinely stronger rounded bound.
This scheduling rule is Primo's design, not an implementation claim about
Pugh's optimized algorithm.

- A local Boolean flag records a nonintegral `rhs / gcd` in `tighten`.
  An exact division does not set it. Reset it after trying the linear
  relaxation; only a subsequent genuine floor reduction enables another try.
- The existing `Active` map supplies primitive rows and their numeric proof
  references. A successful linear witness is scaled to integer coefficients
  and appended as the existing divisor-one cut. Independent replay still
  checks the full ancestor proof against original premises.
- Call once initially, then after a completed projection only if new rounding
  occurred and no opposing/constant contradiction already terminated it.
  Cancellation and all existing row, step, pair and digit caps remain.
- Add a three-variable parity example whose first projection produces three
  collectively inconsistent bounds without any opposing pair. With its pair
  budget exhausted, success must use the second linear check, not another
  projection. Existing `lia-linear-checks` and pivot counters expose this path.
- The additional LP checks have their existing reconstruction cost and can
  occur at most once per completed variable elimination. This is still bounded
  incomplete reconstruction; no dark shadows, splinters or branch replay are
  added. These were candidate proof obligations, not accepted capabilities.


### Measured decision and next producer work

Candidate `e90bf22e` passed its new three-variable, three-pair unit regression
in SLURM `362742`. The target again stopped at exactly 5,224 cuts and 5,186
pairs, with one linear check and no opposing contradiction. The extra linear
checks did not trigger after a completed projection on this target. Neither
candidate resolved the producer failure; both source changes and their active
unit-test additions were removed from the working branch. No resource cap was
raised, and no checker implementation was inspected or changed.

The {download}`candidate ledger <../coverage/lia-reconstruction-followup.json>`
records builds, focused probes, limits and binary hashes. The
{download}`experiment guide <../experiments/lia-reconstruction-20261106/README.md>`
and the {download}`opposing-bound patch <../experiments/lia-reconstruction-20261106/opposing-bounds.patch>`
and {download}`linear-recheck patch <../experiments/lia-reconstruction-20261106/linear-rechecks.patch>`
preserve the rejected mechanisms and their focused tests.
Full native, sanitizer, static-analysis and fuzz gates were not repeated for
these rejected candidates. They are not qualified replacements for the
accepted producer. The unchanged native binary's prior qualification remains
separate from the successful exported-CNF workflow qualification.

The remaining producer work is a reconstruction strategy that can recover
this selected integer conflict without that projection blow-up, or numeric
proof provenance from the main integer reasoning. The present measurements do
not decide which approach will work. They do show that adding the two local
checks above is insufficient for this instance. Merely increasing the cap was
not tested as a remedy. The checker-specific Boolean evaluation errors and
stage timeouts remain feedback items, outside these producer experiments.
