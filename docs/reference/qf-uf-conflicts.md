# QF_UF conflict-output format, version 1

**Status: format design only.** Primo does not yet export or read this format.
This page specifies an output artifact for a supplied CNF problem and recorded
theory conflicts. It is intended for authors of independent producers and
consumers. Producing the artifact does not validate the conflicts or establish
UNSAT. There is no new command-line option or C++ API in this milestone.

The supplied CNF is the starting problem. Converting an earlier formula into
CNF, and establishing the relationship between those formulas, are future work.
Conflict derivations, a checker, and a propositional refutation are also deferred.

## Container and literal syntax

A file contains exactly one S-expression, with these sections occurring once in
the order shown. Keywords and the version are literal tokens; `*` below means
zero or more entries and is not part of a file.

```text
(primo-conflicts
  (version 1)
  (logic QF_UF)
  (signature declaration*)
  (cnf (clause literal*)*)
  (conflicts (conflict literal*)*))
```

The wrapper is custom syntax, not an SMT-LIB command script. Inside it, use
SMT-LIB lexical conventions, including whitespace, semicolon line comments, and
quoted symbols such as `|left endpoint|`. Section names are reserved only at
their structural positions. Version 1 has no additional sections or attributes.

The signature contains `declare-sort` commands for uninterpreted sorts of arity
zero and `declare-fun` commands for constants and functions. `Bool`, `true`,
`false`, equality, and Boolean operators have their usual SMT-LIB meanings.
Declare sorts before functions using them. Declare each user symbol once and
declare every symbol referenced anywhere in the CNF or conflicts, including
auxiliary symbols. Use the same signature for both sections. No definitions,
quantifiers, arithmetic, assertion-stack commands, or solver commands occur in
the container.

A literal is a ground Boolean atom or `(not atom)`. An atom is `true`, `false`,
a Boolean constant or uninterpreted Boolean-valued application, or binary
equality `(= term term)` between terms of the same sort. Terms must be ground
and well-sorted QF_UF terms; Boolean arguments and Boolean equality retain their
SMT-LIB meaning. Boolean expressions inside term arguments do not introduce
extra outer clause connectives. At clause level, use the wrapper's literal
sequence rather than an `or` term. Express disequality as `(not (= s t))`;
`distinct` and nested outer negations are not literal forms in version 1.

## Meaning and polarity

The CNF denotes the conjunction of its clauses. A clause denotes the disjunction
of its literals. An empty CNF is `true`; `(clause)` denotes `false`.

A conflict records **conflicting facts**: its literals are claimed to have an
inconsistent conjunction in QF_UF, independently of the input clauses. For
example:

```lisp
(conflict (= a b) (not (= (f a) (f b))))
```

The facts say that `a` and `b` are equal but their function results differ.
Congruence makes this impossible. Negating each fact gives the corresponding
theory-valid clause:

```lisp
(or (not (= a b)) (= (f a) (f b)))
```

For a conflict sequence with literals l₁, …, lₙ, the intended theory lemma is
¬l₁ ∨ … ∨ ¬lₙ. Negation here is semantic complementation: complementing
`(not atom)` yields `atom`. The conflict itself is not a clause to conjoin with
the input CNF. No explanation steps, equality paths, or congruence witnesses
accompany it.

An empty `(conflict)` claims that the theory alone is inconsistent. It is
structurally representable but is not a valid conflict in ordinary QF_UF.
Similarly, syntactic well-formedness does not make any other recorded conflict
valid. An empty conflicts section records no conflicts and makes no SAT claim.

Preserve literal order, record order, duplicates, and repeated conflicts.
They have no logical significance; version 1 requires no sorting, minimization,
or deduplication. Conflicts may mention declared atoms absent from the input
CNF. They must still be inconsistent without assuming input clauses.

The intended eventual use is that the input CNF together with the complemented
conflict sequences is propositionally UNSAT. Version 1 neither demonstrates
that property nor guarantees that the recorded collection is sufficient.

## Examples and boundary cases

This complete artifact records a congruence conflict:

```lisp
(primo-conflicts
  (version 1)
  (logic QF_UF)
  (signature
    (declare-sort U 0)
    (declare-fun a () U)
    (declare-fun b () U)
    (declare-fun f (U) U))
  (cnf
    (clause (= a b))
    (clause (not (= (f a) (f b)))))
  (conflicts
    (conflict (= a b) (not (= (f a) (f b))))))
```

The downloadable {download}`extended example <../examples/qf-uf-conflicts.sexp>`
also covers transitivity, opposing equality and disequality, multiple sorts,
predicates, Boolean arguments, a quoted name, duplicate literals, and repeated
conflicts. Its input CNF already contains the congruence contradiction above;
the additional conflicts illustrate the format and need not form a minimal set.

These are complete structural boundary examples:

```lisp
; Empty conjunction: true. No recorded conflicts.
(primo-conflicts
  (version 1) (logic QF_UF) (signature) (cnf) (conflicts))

; Input CNF contains false, so it needs no theory lemmas.
(primo-conflicts
  (version 1) (logic QF_UF) (signature) (cnf (clause)) (conflicts))

; Well-formed container, but its empty conflict is an invalid theory claim.
(primo-conflicts
  (version 1) (logic QF_UF) (signature) (cnf) (conflicts (conflict)))
```

Each S-expression above is a separate file. Semantic review cases for a future
consumer include reversing a conflict's polarity, omitting a required equality
premise, undeclared symbols, and ill-sorted equality. These must not be treated
as valid theory evidence merely because parentheses balance.

## Representation and future integration

The public representation is typed term text and literal sequences. It contains
no Primo term IDs, SAT variable numbers, flattened terms, congruence-class
representatives, or references to solver memory. There is no ordering or
numbering dependency on Primo's allocation history. An independent consumer
needs only this specification and SMT-LIB term semantics.

Internally, {source}`BooleanClausifier <src/smt/clausifier.hpp>` keeps the mapping
between SAT literals and core terms in the SMT layer. The
{source}`term printer <src/smtlib/term_printer.hpp>` provides existing term-text
utilities; it is not an exporter for this format. A future producer adapter
belongs at that SMT boundary, with syntax printing in the SMT-LIB layer.
SAT must continue to see propositional literals only.

Primo's [EUF explanations](../implementation/euf.md#follow-one-conflict) use
learned-clause polarity: reasons already negate the asserted assignments.
A future adapter must complement those literals to produce the conflicting
facts specified here. It must map every literal back to a declared term;
selectors and unmapped SAT variables cannot be printed as theory facts.

Collecting enough conflicts for a later refutation also requires accounting
for theory propagation reasons and other theory-valid clauses used by SAT.
Recording only terminal conflicts need not suffice. SAT-learned clauses are
not automatically theory conflicts and must not be placed in this section.
Preprocessing, auxiliary declarations, incremental queries, and a complete
collection strategy require a separate exporter design before implementation.

This page is an original output-format design note, not an implementation of a
paper-backed proof algorithm. No checker, exporter, or new solver behavior is
implemented here.
