# Proposed weighted certificates for QF_LRA conflicts

**Status: experimental implementation with bounded SLURM qualification.**
The [version-2 reference](../reference/qf-lra-certificates.md) describes the
implemented producer and independent arithmetic validator. This page retains
the design argument and acceptance criteria. Primo emits no source-to-CNF
conversion proof; the separate [Micromath source-based route](../reference/qf-lra-certificates.md#micromath-verification-from-the-original-source)
now supplies externally kernel-checked proofs for the qualified small LRA set.
Extend the [single-query CNF/conflict format](../reference/qf-uf-conflicts.md)
with arithmetic conflicts carrying exact rational multipliers. The objective
is an independently checkable artifact with a small arithmetic inference rule,
while preserving the existing separation between exported CNF, theory lemmas
and propositional refutation. Incremental solving remains outside this proposal.

The [CPC reading note](../papers/cooperating-proof-calculus.md), especially its
discussion of §3.5, motivates scaling, summation and normalization as separate
proof obligations. We can check those operations directly for one compact
record. Expanding it into a general proof language or a large trace of
intermediate inequalities is unnecessary for this first extension.

**Production strategy:** collect theory clauses during the normal solve,
select the retained clauses with the existing propositional CORE/MUS oracle,
then reconstruct weights only for those retained clauses. No coefficient
history is required in the main LRA search. This order is implemented; the
[reference](../reference/qf-lra-certificates.md#independent-validation-and-qualification-scope)
records the bounded correctness checks.

## Proposed version 2 syntax and meaning

Use an explicit version increment; do not reinterpret version 1. Initially,
version 2 adds the `QF_LRA` logic, declarations of Real and Boolean constants,
linear arithmetic atoms in clauses, and `conflict-lra` records. The following
is the version-2 output syntax:

```lisp
(primo-conflicts
  (version 2)
  (logic QF_LRA)
  (signature
    (declare-fun x () Real)
    (declare-fun y () Real))
  (cnf
    (clause (<= (+ x y) 2))
    (clause (>= x 2))
    (clause (> y 0)))
  (conflicts
    (conflict-lra
      ((<= (+ x y) 2) 1)
      ((>= x 2)       1)
      ((> y 0)        1))))
```

A record is a sequence of `(literal coefficient)` pairs. Its literals are
**conflicting facts**, following version 1's polarity convention, rather than
the clause to assert. If its literals are $l_1,\ldots,l_n$, successful checking
justifies the theory lemma $\neg l_1\lor\cdots\lor\neg l_n$. These facts must be
inconsistent independently of other input clauses. Propositional refutation
of the CNF plus the justified lemmas remains a separate check.

Preserve the actual signed literals used at the SAT/export boundary. In
particular, `(not (<= x 2))` stays that literal in the record even though the
arithmetic checker interprets it as $x>2$. Do not silently replace it by an
unrelated SAT atom spelling the equivalent inequality. No new atom identifiers
are needed for this initial syntax. Repeated pairs add their contributions;
record order has no mathematical significance.

Coefficients are nonnegative exact rationals: integer numerals or `(/ N D)`
with nonnegative integer numerator and positive integer denominator. No decimal
floating-point approximation is permitted. The initial producer retains the
selected fact sequence, including premises whose reconstructed coefficient is
zero. This preserves the exact selected theory clause; zero coefficients
contribute neither a numeric sum nor strictness. An empty record cannot pass
the arithmetic contradiction check. Affine term constants may additionally be signed rationals, and SMT-LIB
decimal constants must have their exact rational meaning.

## Independent arithmetic check

The initial arithmetic literal vocabulary is a binary comparison using `<`,
`<=`, `>` or `>=`, or the negation of such a comparison. Both operands must be
affine Real expressions over declared constants. Boolean constants and their
negations remain available as propositional CNF literals, but are not numeric
premises for `conflict-lra`. Nonlinear products, integer-specific reasoning,
mixed Int/Real arithmetic and uninterpreted function applications are outside
this initial certificate fragment.

The checker derives a sparse affine form from each literal itself. It collects
terms and constants using exact rational arithmetic, normalizes comparison
direction to

$$
a_i\cdot x\le b_i\quad\text{or}\quad a_i\cdot x<b_i,
$$

and records whether the comparison is strict. Negation complements the
comparison: for example, $\neg(s\le t)$ is $s>t$, whereas
$\neg(s<t)$ is $s\ge t$. Multiplication inside an affine expression is allowed
only when it is linear, such as multiplication by a rational constant; the
checker must reject a nonlinear expression rather than treating it as an
uninterpreted arithmetic variable. Division must have a nonzero rational
constant denominator.

For the recorded multipliers $\lambda_i$, compute

$$
A=\sum_i\lambda_i a_i,\qquad
B=\sum_i\lambda_i b_i,\qquad
S=\bigvee_i(\lambda_i>0\ \land\ \text{premise }i\text{ is strict}).
$$

Accept exactly when every multiplier is nonnegative, every component of $A$
is zero, and

$$
B<0\quad\lor\quad(B=0\ \land\ S).
$$

When $S$ is false, the combination asserts $0\le B$, which is false if $B<0$.
When $S$ is true, the combination asserts $0<B$, which is false if $B\le0$.
This proves soundness of the proposed arithmetic checking rule directly. A
strict premise with coefficient zero must not set $S$: that would, for example,
allow a satisfiable strict inequality to certify $0<0$ by itself.

In the example, normalize $x\ge2$ to $-x\le-2$ and $y>0$ to $-y<0$.
The three unit-weight premises sum to $0<0$. For a non-unit example,
$2x+y\le3$, $x\ge2$ and $y\ge0$, with respective weights $1,2,1$,
sum to $0\le-1$.

The checker owns an accumulator from variable identities to rational
coefficients, a rational constant sum and the strictness flag. It can process
premises successively and discard each temporary affine form. Storage depends
on the active affine support and exact-number sizes, not on a sequence of
intermediate proof steps. Rational bit sizes and the term-normalization
strategy determine cost; no constant-memory or linear-bit-complexity claim is
made. Transit through a pipe does not bound these internal data structures.

## Equalities and disequalities

For the first producer, reuse Primo's existing Real-equality lowering:

$$
s=t\ \longleftrightarrow\ (s\le t)\land(t\le s).
$$

Negated equality becomes a Boolean choice between strict inequalities, handled
by the existing SAT/clausification pipeline. Export that prepared CNF and its
actual inequalities; do not add another equality-lowering transformation in
the exporter. The [LRA implementation](../implementation/lra.md) describes
this existing solver behavior. Proving the source-to-prepared-CNF
transformation is still separate future work.

Preserving equality premises is an optional later extension. An equation
$a\cdot x=b$ can carry a signed rational multiplier, since either sign preserves
equality; its contribution adds to $A$ and $B$ but never to $S$. Disequality
cannot be added as a single linear inequality: it needs Boolean splitting or
another explicitly justified inference. Do not silently treat it as equality
or choose one of its branches inside the arithmetic checker.

## Producer integration and resource behavior

The former UF collector has been generalized to
{lraexportsource}`CnfConflictCollector <src/smt/cnf_export.hpp>`. The existing
{ref}`LRA conflict construction <a-bound-contradiction-and-its-cancellation-proof>`
returns premise literals without weights; the new collector reconstructs
selected witnesses after propositional reduction.

The witness must be reconstructed from the selected exported facts themselves,
including their exact coefficients, constants and signed-literal polarity.
Internal slack definitions or derived bounds are not additional assumptions.
Reconstructing from these facts avoids depending on the main tableau's current
basis, its pivot history or its earlier normalization scale. SAT sees only the
lemma clause; the export-side reconstruction context owns coefficient search,
and the independent checker validates the resulting weighted combination.

Keep record serialization incremental using the existing
{micromathpipesource}`ConflictSpool <src/smtlib/conflict_spool.hpp>` ownership
model rather than accumulating formatted proof strings. The SAT-based CORE/MUS
oracle continues selecting whole theory clauses against the fixed input CNF;
the selected records receive witnesses afterwards. Selectors remain private to
reduction. Literal-level conflict minimization is a different operation.

## Reconstructing coefficients after lemma selection

This is the preferred initial design. The reduction oracle needs the numeric
SAT clauses, not arithmetic witnesses. Each retained clause must independently
be valid in LRA, so its complemented facts form an inconsistent arithmetic
conjunction without the rest of the input CNF or the main solver's trail.
Reconstructing only these conjunctions avoids constructing and retaining
weights for lemmas discarded by CORE/MUS. It trades that work and storage for
an additional exact feasibility problem per retained lemma; a speed or total
memory improvement must be measured, not assumed.

### Export phases and retained state

1. **Solve and collect.** Capture the prepared CNF and the complete theory
   conflict/reason clauses actually sent to SAT. Keep stable record indices and
   query-owned SAT-variable-to-atom bindings. Preserve the term manager until
   reconstruction and final writing finish. Collect literals, not snapshots of
   tableau states or histories of coefficients.
2. **Select whole lemmas.** Run the existing propositional CORE/MUS reduction
   against the fixed CNF. For `none`, retain all collected records. For an empty
   selected set, perform no coefficient reconstruction. Arithmetic checking is
   not the reduction oracle: every valid theory lemma would be redundant to an
   SMT solver with the same theory available.
3. **Reconstruct one retained record.** Read its numeric literals, complement
   them into facts, and derive their exact affine forms and strictness flags
   using the original atom bindings. Search for weights in a fresh arithmetic
   context, using no other input clauses, collected lemmas or main-solver
   bounds. Propagation reasons use their antecedents plus the negated
   conclusion, exactly as specified above.
4. **Check and stage.** Pass the candidate to the small exact accumulator
   checker, which checks the facts rather than trusting the reconstruction
   context's private rows. Stream the checked weighted record to a private
   output section, release its temporary matrix/tableau/weight vector, then
   process the next selected record.
5. **Publish.** Only after every selected witness checks, assemble the version-2
   signature, unchanged CNF and weighted conflicts. A later independent
   propositional refutation must use the same clauses and atom mapping. The
   artifact's completeness does not certify source-to-CNF conversion.

CORE/MUS can reuse the existing retained numeric CNFs. For `none`, do not add
an unbounded in-memory vector of every clause merely to defer reconstruction:
stage numeric lemma records in a query-owned temporary spool and read them
successively. A private numeric spool belongs in the SMT/export layer, where
SAT literals have meaning; the existing generic text `ConflictSpool` must not
gain SAT or arithmetic dependencies. Do not parse expanded SMT-LIB text back
into clauses to feed reconstruction. Weighted output remains disk-backed;
string accumulation is unnecessary in either mode.

### Recovering weights as an ordinary exact feasibility model

Let a retained record have $m$ normalized premises, with coefficient matrix
$M\in\mathbb Q^{m\times n}$, right-hand-side vector $b\in\mathbb Q^m$,
and strictness vector $s\in\{0,1\}^m$. Entry $s_i$ is one exactly when premise
$i$ is strict. Use sparse rows and only variables occurring in this record;
do not allocate a matrix indexed by the whole query's term-manager capacity.
Introduce one unknown multiplier $\lambda_i$ per premise and
solve the following **non-strict rational linear feasibility** problem:

$$
\lambda\ge0,\qquad M^T\lambda=0,\qquad
b^T\lambda\le0,\qquad (s-b)^T\lambda=1.
$$

This normalization is our construction from the certificate criterion, not a
new rule required in the public format. Write $B=b^T\lambda$ and
$T=s^T\lambda$. Nonnegativity gives $T\ge0$. The last two constraints imply
$B\le0$ and $T-B=1$, hence either $B<0$ or $B=0$ and $T>0$. The resulting
weights therefore satisfy the independent arithmetic check. Conversely, any
valid weighted contradiction has $T-B>0$; dividing all its weights by that
positive rational makes $T-B=1$ without changing cancellation or validity.
Thus the normalization does not lose witnesses.

Here $M$, $b$ and $s$ belong to the temporary reconstruction context. The
external arithmetic validator must derive its own affine forms from the
serialized facts; rechecking the producer's already-normalized matrix would
not independently validate that matrix's connection to the artifact.

Motzkin's transposition theorem supplies the existence of such weights for
inconsistent conjunctions of strict and non-strict linear inequalities over
the supported rational/real domains; see Leike and Heizmann,
*Ranking Templates for Linear Loops*, §6.1, Theorem 6.1
{cite:p}`LeikeHeizmann2015Ranking`. The reading scope for that citation is the
field convention in §2 and §6.1, not the paper's ranking-template algorithms.
Our normalized system is linear because all coefficients of the exported
premises are fixed rationals. No optimization objective is needed.

The proposed first implementation builds this private feasibility system using
the existing {micromathpipesource}`LraTableau
<src/theory/arithmetic/lra_tableau.hpp>` machinery: multiplier variables,
non-strict lower bounds, rows for the equalities and auxiliary affine
expressions, then `check()` and `assignment()`. Its satisfying assignment is
the certificate weight vector. It does not ask `check()` for a certificate of
its own infeasibility, so it needs no coefficient-provenance extension to the
main conflict API. Because this new system has only non-strict rational bounds,
its candidate weights must be ordinary rationals; reject any unexpected
nonzero infinitesimal component before using them. Source SAT literals and
private feasibility reason labels must stay in separate namespaces.

Concretely, allocate all multiplier variables and rows before asserting
bounds. For each column $j$ of $M$, define an auxiliary variable
$r_j=\sum_i M_{ij}\lambda_i$ and fix its lower and upper bounds to zero.
Define $r_B=\sum_i b_i\lambda_i$ with upper bound zero and
$r_N=\sum_i(s_i-b_i)\lambda_i$ with both bounds equal to one. Give each
$\lambda_i$ lower bound zero. This uses `add_variable`, `add_row`,
`assert_lower` and `assert_upper`; nonzero constants are bounds, so the
tableau's homogeneous-row representation needs no extension. Either an
immediate bound contradiction or a conflict from `check()` means no model
was obtained, not a weight vector. The first reconstruction context can use
the existing Bland policy, avoiding a new heuristic or performance claim.

`LraTableau::check()` now accepts an optional cooperative cancellation callback
inside its pivot loops and throws `LraCheckInterrupted` when it fires.
Reconstruction catches this as an explicit interrupted status; it never treats
interruption as `check()`'s successful `nullopt` result. Setup and model extraction
also poll the phase callback. Checks only between records would be insufficient.
The native tests interrupt inside one feasibility check and exercise preservation
of an existing artifact when export reconstruction is cancelled. Cooperative checks do
not promise a hard real-time bound on an individual exact-number operation;
external process/allocation limits remain the final bound.

For the strict example above, $\lambda=(1,1,1)$ has $B=0$ and $T=1$. For the
non-unit example, $\lambda=(1,2,1)$ has $B=-1$ and $T=0$. Both already satisfy
the normalization. Satisfiable premise sets, including a strict inequality
given zero weight, must have no accepted reconstructed witness.

### Clause identity, failure and cancellation

A reconstructed vector may give some premises zero weight. Keep those
premises in the emitted record initially: dropping them strengthens the theory
clause. Although independently justified strengthening can preserve UNSAT, it
changes the clause selected by CORE/MUS, can alter minimality and can invalidate
a pre-existing propositional proof's clause references. Literal-level shrinking
would require a separate design and rechecking. The external arithmetic
checker must handle zero weights without giving them strictness.

Reconstruction failure must not silently drop a selected lemma, substitute
unchecked weights or borrow assumptions from the original problem. An
infeasible reconstruction problem for an allegedly valid supported lemma is a
diagnostic requiring investigation of the lemma, normalization or implementation.
Timeout, cancellation and unsupported input are inconclusive, not evidence
that the original query is SAT. Preserve the main-UNSAT status in diagnostics,
report certificate generation as incomplete, and do not publish a completed
artifact. Stage witnesses before opening the final output path; preserve the
existing documented limitations on failures during final publication itself.

For the initial implementation, use one fresh reconstruction context at a
time. Record selected/reconstructed/failed lemma counts, premise counts,
feasibility dimensions and nonzeros, pivot counts, largest coefficient sizes,
phase times and peak memory. Bound reconstruction separately from main solving
and CORE/MUS so postprocessing cannot be mistaken for failure to establish
main UNSAT. Direct weight capture in the main solver remains a later option
if measurements show that reconstruction dominates.

An external checker should compute the weighted combination directly. A future
CPC translator may elaborate it into scaling, summation and normalization steps;
no CPC translator is implemented. The separate
[Micromath route](../reference/qf-lra-certificates.md#micromath-verification-from-the-original-source)
now checks a bounded version-2 LRA set against statements built from the original
SMT-LIB, independently of the producer CNF.
Pin the inference semantics/version independently of any checker binary.

## Implementation checklist and acceptance criteria

Before describing an LRA exporter as implemented:

1. Define version-2 parsing, exact rational syntax, supported affine expressions
   and rejection behavior. Keep version-1 interpretation unchanged.
2. Reconstruct exact weights after whole-lemma selection, using only the
   selected exported signed atoms. Cover direct-bound, blocked-row and any
   enabled alternative conflict paths, without retaining their original
   tableau histories. Explain normalization and private feasibility setup. Include
   theory-propagation reason clauses: a justification $P\Rightarrow l$ can be
   certified by recording the premises $P$ together with $\neg l$ as conflicting
   facts. State explicitly which propagation modes the first producer supports;
   do not export unproved reason clauses from an unsupported path.
3. Check every emitted conflict with an independent exact accumulator, then
   check propositional UNSAT separately. Do not count a solver exit status or
   unchecked conflict as a completed certificate.
4. Add negative controls for negative inequality weights, coefficient changes,
   uncancelled variables, incorrect constants, polarity changes, zero-weight
   strict premises, invalid rationals and nonlinear input. Exercise both
   $B<0$ and strict $B=0$ contradictions, already-lowered equalities and SAT
   disequality branches. These validate the rule specification as well as its
   implementation.
5. Retain streaming, cancellation and failure behavior; verify that CORE/MUS
   preserves clause identities, discarded records trigger no reconstruction,
   zero-weight premises remain in selected records, and interrupted or failed
   reconstruction cannot publish a success.
6. Record collected/retained conflicts, nonzero witness entries, coefficient
   sizes, certificate bytes and per-stage/checker memory/time. Run the required
   native, sanitizer and fresh fuzz gates on the implemented revision, followed
   by bounded SLURM certificate replays on known-solved real LRA benchmarks.

The producer artifact alone starts from the exported CNF. It does not
prove Primo's preprocessing or clausification, solve LIA,
verify an entire checker, or introduce incremental certification.

Implement in reviewable milestones: first the independent version-2 arithmetic
validator and rejection tests; then isolated post-selection reconstruction,
its interruption mechanism and exact feasibility tests; then SAT-boundary LRA
collection, numeric spooling
and CORE/MUS integration; finally the required correctness gates and real-input
SLURM evaluation. Keep the existing CLI shape: `--export-cnf FILE` and
`--export-cnf-reduction=none|core|mus`, with one query and no new coefficient
strategy option for the initial producer. Version 1 remains the QF_UF path;
version 2 is planned for QF_LRA. No default reduction change is proposed.

## Sources and implementation status

Reynolds et al., *The Cooperating Proof Calculus: Comprehensive Proofs for an
SMT Solver* {cite:p}`ReynoldsEtAl2026CPC`, §§2.2–2.3, 3.2–3.5 and 4, is the
primary design reference for scoped theory proofs, arithmetic operations,
normalization and independent rule validation. The exact `conflict-lra` record,
accumulator check, normalized reconstruction system and integration checklist
above are our proposal, not CPC
syntax or a claim that CPC's whole arithmetic calculus is implemented.
The existing QF_UF/version-1 export retains its format. The version-2 producer,
arithmetic validator and selected witness export are implemented with bounded
qualification. Primo has no proof-language emitter; the external Micromath
workflow and its bounded coverage are documented separately. Motzkin's theorem is a
mathematical basis for witness reconstruction, not a claim to implement
Leike–Heizmann's synthesis procedure or an optimized LP algorithm from that paper.
