# Proposed weighted certificates for QF_LRA conflicts

**Status: format proposal; no LRA certificate producer or checker implemented.**
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

## Proposed version 2 syntax and meaning

Use an explicit version increment; do not reinterpret version 1. Initially,
version 2 adds the `QF_LRA` logic, declarations of Real and Boolean constants,
linear arithmetic atoms in clauses, and `conflict-lra` records. The following
is illustrative proposed syntax, not an input accepted by current Primo:

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
floating-point approximation is permitted. The exporter should omit zero
coefficients; a checker may accept them, but they contribute neither a numeric
sum nor strictness. An empty record cannot pass the arithmetic contradiction
check. Affine term constants may additionally be signed rationals, and SMT-LIB
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

The current {micromathpipesource}`QfUfConflictCollector
<src/smt/qf_uf_export.hpp>` exports QF_UF facts without arithmetic witnesses;
{ref}`LRA conflict construction <a-bound-contradiction-and-its-cancellation-proof>`
returns premise literals without exporting multipliers. Adding a format tag
alone does not supply the required arithmetic evidence.

An implementation must preserve the coefficients and scaling that connect a
conflict to its exported atoms. Expand internal slack definitions and derived
bounds into justified exported premises. An internal tableau identity or bound
must not become an unproved extra assumption. SAT sees only the resulting
lemma clause; arithmetic witness construction belongs to the theory/SMT
explanation boundary, and serialization belongs to the export layer.

Keep record serialization incremental using the existing
{micromathpipesource}`ConflictSpool <src/smtlib/conflict_spool.hpp>` ownership
model rather than accumulating formatted proof strings. The SAT-based CORE/MUS
oracle should continue selecting whole theory clauses against the fixed input
CNF; retained records carry their witnesses unchanged. Selectors remain private
to reduction. Literal-level conflict minimization is a different operation.

An external checker should compute the weighted combination directly. A future
translator may elaborate it into CPC scaling, summation and normalization steps,
but no such translator or CPC/Micromath LRA compatibility is claimed here.
Pin the inference semantics/version independently of any checker binary.

## Implementation checklist and acceptance criteria

Before describing an LRA exporter as implemented:

1. Define version-2 parsing, exact rational syntax, supported affine expressions
   and rejection behavior. Keep version-1 interpretation unchanged.
2. Produce exact weights over exported signed atoms, including direct-bound,
   blocked-row and any enabled alternative conflict paths. Explain each
   normalization/scaling boundary and internal-definition expansion. Include
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
   preserves witnesses and that interrupted runs cannot publish a success.
6. Record collected/retained conflicts, nonzero witness entries, coefficient
   sizes, certificate bytes and per-stage/checker memory/time. Run the required
   native, sanitizer and fresh fuzz gates on the implemented revision, followed
   by bounded SLURM certificate replays on known-solved real LRA benchmarks.

The certified starting point remains the exported CNF. This extension does not
prove the original SMT script's preprocessing or clausification, solve LIA,
verify an entire checker, or introduce incremental certification.

## Sources and implementation status

Reynolds et al., *The Cooperating Proof Calculus: Comprehensive Proofs for an
SMT Solver* {cite:p}`ReynoldsEtAl2026CPC`, §§2.2–2.3, 3.2–3.5 and 4, is the
primary design reference for scoped theory proofs, arithmetic operations,
normalization and independent rule validation. The exact `conflict-lra` record,
accumulator check and integration checklist above are our proposal, not CPC
syntax or a claim that CPC's whole arithmetic calculus is implemented.
The existing QF_UF export and LRA solver retain their documented behavior;
the version-2 producer, arithmetic certificate checker, witness export and
optional proof-language translation are unimplemented.
