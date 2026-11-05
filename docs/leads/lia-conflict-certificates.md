# Proposed integer-only LIA conflict derivations

**Status: format proposal with a hand-written example; no LIA certificate
exporter or checker is implemented.** The existing UF/version-1 and
[LRA/version-2 formats](../reference/qf-lra-certificates.md) remain unchanged.
The proposed version 3 adds `QF_LIA`, `Int` constants and `conflict-lia` records.
The exported CNF remains the starting problem; this proposal supplies no proof
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

| Property | Current LRA record | Proposed LIA record |
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

## Scope and integration direction

This is an initial cut-derivation format, not a claim that current native LIA
conflicts can already be replayed. The current
{ref}`integer explanation <lia-explanations>` is represented by
{lraexportsource}`LiaExplanation <src/theory/arithmetic/lia_explanation.hpp>`, which stores
antecedent literals from cuts, GCD checks and exhausted branches, without an
external derivation. Supporting that solver boundary needs proof production or
proof reconstruction. Direct branch-and-bound replay additionally needs a
scoped integer split rule and verification of both closed branches; it is not
defined by this initial syntax.

Keep the existing post-CORE/MUS objective: select complete theory clauses first,
then generate the derivation only for retained clauses. This is a planned LIA
extension, not something achieved by merely rescaling the existing LRA weights.
The old starter archive contains actual version-2 outputs and remains unchanged;
the files linked here are explicitly hand-written version-3 proposals.

## Sources and implementation status

Nonnegative linear combination is the same mathematical operation used by the
[LRA proposal](lra-conflict-certificates.md#independent-arithmetic-check).
Integer coefficient normalization and bound tightening are described in
Pugh's expanded *Omega Test*, §2.1 {cite:p}`pugh1992omega`. Our `cut` syntax
combines those operations into one explicit, checked inference; this is not an
implementation of the Omega algorithm. Names, record layout and the example
are our design proposal. No version-3 parser, serializer, derivation generator
or checker has been added.
